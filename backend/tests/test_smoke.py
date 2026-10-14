# -*- coding: utf-8 -*-
"""
冒烟测试: 确认服务能起来、主要接口能通。
=====================================================================
跑法(在 backend/ 目录下):
    .\\.venv\\Scripts\\python.exe -m pytest tests -v

★ 这些测试不依赖 MQTT broker, 也不依赖硬件:
  连不上 broker 只会打一条警告, 接口该返回什么还是返回什么 ——
  这正是"优雅降级"该有的样子, 也是后端能独立开发的原因。
=====================================================================
"""
import uuid

import pytest
from fastapi.testclient import TestClient

from app.main import app
from app.db.models import CameraFrame
from app.db.session import SessionLocal
from app.services import camera_service, device_service


def _uid(tag: str) -> str:
    """
    生成一次性 device_id。

    ★ 为什么必须这样做: 数据库是持久化的。如果每次测试都用固定的 device_id,
      第二次运行时库里已经躺着上一次的数据, 「历史记录正好 3 条」这类断言
      就会莫名其妙地失败。测试必须能反复运行, 且结果不依赖运行次数 ——
      这就是"幂等"的意思。
    """
    return f"pytest-{tag}-{uuid.uuid4().hex[:8]}"


@pytest.fixture(scope="module")
def client():
    # 用 with 才会触发 lifespan(启动/关闭钩子), 与真实运行一致
    with TestClient(app) as c:
        yield c


@pytest.fixture(autouse=True)
def reset_camera_discovery():
    """相机地址来自 MQTT 内存缓存；每个测试都从未发现相机的状态开始。"""
    with camera_service._lock:
        camera_service._endpoint.stream_url = ""
        camera_service._endpoint.snapshot_url = ""
    device_service._status_cache.pop("cam01", None)
    yield
    with camera_service._lock:
        camera_service._endpoint.stream_url = ""
        camera_service._endpoint.snapshot_url = ""
    device_service._status_cache.pop("cam01", None)


def test_health(client):
    """健康检查必须永远能通 —— 前端靠它判断后端在不在。"""
    r = client.get("/api/health")
    assert r.status_code == 200
    body = r.json()
    assert body["status"] == "ok"
    assert "mqtt_connected" in body


def test_device_status(client):
    r = client.get("/api/device/status")
    assert r.status_code == 200
    assert "online" in r.json()


def test_camera_offline_status_and_live_proxy_returns_503(client):
    """没有 MQTT 服务发现时，视觉页应得到可解释的离线结果而不是空白画面。"""
    status = client.get("/api/vision/camera/status")
    assert status.status_code == 200
    assert status.json()["online"] is False
    assert status.json()["stream_available"] is False

    live = client.get("/api/vision/live")
    assert live.status_code == 503
    assert "相机离线" in live.json()["detail"]


def test_camera_status_discovery_and_capture_metadata(monkeypatch):
    """JPEG 只用于统计字节数；数据库中必须只留下 stored=false 的元数据。"""
    camera_service.update_from_status(
        {
            "device_id": "cam01",
            "stream_url": "http://192.168.4.20/stream",
            "snapshot_url": "http://192.168.4.20/capture",
        }
    )
    endpoint = camera_service.endpoint()
    assert endpoint.stream_url.endswith("/stream")

    import httpx

    request = httpx.Request("GET", endpoint.snapshot_url)
    response = httpx.Response(
        200,
        headers={"content-type": "image/jpeg"},
        content=b"jpeg-bytes-are-not-saved",
        request=request,
    )
    monkeypatch.setattr(camera_service.httpx, "get", lambda *args, **kwargs: response)

    row = camera_service.capture_metadata()
    assert row.captured is True
    assert row.stored is False
    assert row.byte_size == len(response.content)
    with SessionLocal() as db:
        saved = db.get(CameraFrame, row.id)
        assert saved is not None
        assert saved.stored is False
        assert saved.byte_size == len(response.content)


def test_camera_capture_failure_is_recorded(monkeypatch):
    """相机断开时也要留下失败原因，便于排查而不会触发 AI。"""
    camera_service.update_from_status(
        {"device_id": "cam01", "snapshot_url": "http://192.168.4.20/capture"}
    )
    import httpx

    monkeypatch.setattr(
        camera_service.httpx,
        "get",
        lambda *args, **kwargs: (_ for _ in ()).throw(httpx.ConnectError("offline")),
    )
    row = camera_service.capture_metadata()
    assert row.captured is False
    assert row.stored is False
    assert "抓帧失败" in row.error


def test_telemetry_roundtrip(client):
    """写一条再读回来, 验证「入库 → 查询」整条链路。"""
    dev = _uid("roundtrip")
    payload = {
        "device_id": dev,
        "soil_moisture": 42.5,
        "soil_ec": 1.3,
        "temperature": 24.0,
        "light_lux": 5000,
    }
    r = client.post("/api/telemetry/simulate", json=payload)
    assert r.status_code == 200
    assert r.json()["saved"] is True

    r = client.get("/api/telemetry/latest", params={"device_id": dev})
    assert r.status_code == 200
    body = r.json()
    assert body["available"] is True
    assert body["data"]["soil_moisture"] == 42.5
    assert body["data"]["device_id"] == dev


def test_history_returns_ascending_order(client):
    """历史数据必须按时间正序, 前端画曲线时不用再倒腾。"""
    dev = _uid("order")
    for v in (30.0, 31.0, 32.0):
        client.post(
            "/api/telemetry/simulate",
            json={"device_id": dev, "soil_moisture": v},
        )

    r = client.get("/api/telemetry/history", params={"device_id": dev})
    assert r.status_code == 200
    rows = r.json()["data"]
    assert len(rows) == 3
    values = [row["soil_moisture"] for row in rows]
    assert values == [30.0, 31.0, 32.0]


def test_command_validation(client):
    """duration_ms 超出上限必须被 Pydantic 拦下来(422), 不能发到板子上。"""
    r = client.post(
        "/api/device/command",
        json={"actuator": "pump", "action": "on", "duration_ms": 999_999_999},
    )
    assert r.status_code == 422


def test_unknown_actuator_rejected(client):
    """不存在的执行器名要被拒绝, 否则板子收到会一脸懵。"""
    r = client.post(
        "/api/device/command",
        json={"actuator": "laser", "action": "on", "duration_ms": 1000},
    )
    assert r.status_code == 422


def test_command_publishes_intent_only(client):
    """
    指令接口在 MQTT 断开时也必须正常返回(不能 500)。

    返回 sent=false 是正确行为 —— 它诚实地说"没发出去",
    而不是假装成功。前端据此提示用户"边缘端或 MQTT 未连接"。
    """
    r = client.post(
        "/api/device/command",
        json={"actuator": "pump", "action": "on", "duration_ms": 3000},
    )
    assert r.status_code == 200
    body = r.json()
    assert "sent" in body
    assert body["command"]["duration_ms"] == 3000


def test_websocket_greeting_and_ping(client):
    """
    WebSocket 连上要能收到问候, 发 ping 要能收到 pong。
    前端 utils/ws.js 依赖这个握手, 所以必须测。
    """
    with client.websocket_connect("/ws") as ws:
        hello = ws.receive_json()
        assert hello["type"] == "hello"

        ws.send_text("ping")
        pong = ws.receive_json()
        assert pong["type"] == "pong"


def test_websocket_supports_multiple_clients(client):
    """
    要能同时接多个客户端(PC 和手机一起看同一个花盆)。
    这里验证两个连接互不干扰。
    """
    with client.websocket_connect("/ws") as a, client.websocket_connect("/ws") as b:
        assert a.receive_json()["type"] == "hello"
        assert b.receive_json()["type"] == "hello"

        a.send_text("ping")
        assert a.receive_json()["type"] == "pong"

        b.send_text("ping")
        assert b.receive_json()["type"] == "pong"

