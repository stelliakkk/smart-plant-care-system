# -*- coding: utf-8 -*-
"""
后端服务入口
=====================================================================
启动(先激活独立虚拟环境, 千万别用 ESP-IDF 的那个 venv):

    cd D:\\Lab\\Embedded Curriculum Design\\pro\\backend
    .\\.venv\\Scripts\\Activate.ps1
    uvicorn app.main:app --reload

接口文档(自动生成, 可以直接点击调试):
    http://127.0.0.1:8000/docs
=====================================================================
"""
import logging
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles

from app.api import routes_device, routes_health, routes_telemetry, routes_vision, ws
from app.config import settings
from app.core.logging import setup_logging
from app.db.session import init_db
from app.mqtt import topics
from app.mqtt.client import mqtt_client
from app.tasks.scheduler import start_scheduler, stop_scheduler

logger = logging.getLogger(__name__)


# ---------------------------------------------------------------------
# MQTT 上行数据的接线板
# ---------------------------------------------------------------------
def _on_telemetry(topic: str, data: dict) -> None:
    """边缘端上报的传感器数据 —— 入库并刷新设备在线状态。"""
    from app.schemas.telemetry import TelemetryPayload
    from app.services import device_service, telemetry_service

    try:
        payload = TelemetryPayload.model_validate(data)
    except Exception as exc:
        logger.warning("上报数据格式不合法, 已丢弃: %s", exc)
        return

    try:
        telemetry_service.save_reading(payload)
        device_service.mark_seen(payload.device_id)
    except Exception:
        logger.exception("处理传感器上报失败")

    # TODO(阶段 2): 通过 ws.manager 把新数据推给前端
    # TODO(阶段 4): 交给 control_service 做阈值闭环决策


def _on_status(topic: str, data: dict) -> None:
    """边缘端心跳。"""
    from app.services import device_service

    try:
        from app.services import camera_service

        previous_boot = device_service.get_status(settings.device_id).boot_id
        device_service.mark_seen(
            device_id=data.get("device_id", settings.device_id),
            rssi=data.get("rssi"),
            uptime_s=data.get("uptime_s"),
            fault=data.get("fault", ""),
            boot_id=data.get("boot_id", ""),
            control_ready=data.get("control_ready", False),
            actuators=data.get("actuators"),
            automation=data.get("automation"),
            automation_ready=data.get("automation_ready", False),
            light_lux=data.get("light_lux"),
            light_fault=data.get("light_fault", ""),
        )
        camera_service.update_from_status(data)
        if data.get("device_id") == settings.device_id:
            from app.services import automation_service
            if previous_boot != data.get("boot_id", ""):
                automation_service.reset_sequence()
            automation_service.observe_light(data)
            automation_service.reconcile()
    except Exception:
        logger.exception("处理心跳失败")


def _wire_mqtt_handlers() -> None:
    """把业务处理函数注册到对应主题上。"""
    edge_id = settings.device_id
    cam_id = settings.camera_device_id
    mqtt_client.subscribe(topics.topic_telemetry(edge_id), _on_telemetry)
    mqtt_client.subscribe(topics.topic_status(edge_id), _on_status)
    mqtt_client.subscribe(topics.topic_status(cam_id), _on_status)
    from app.services import command_service
    mqtt_client.subscribe(topics.topic_command_ack(edge_id), command_service.acknowledge)
    logger.info("MQTT 处理函数已注册 (edge=%s, camera=%s)", edge_id, cam_id)


# ---------------------------------------------------------------------
# 生命周期
# ---------------------------------------------------------------------
@asynccontextmanager
async def lifespan(app: FastAPI):
    setup_logging(settings.log_level)
    logger.info("=" * 66)
    logger.info("  基于大模型的植物养护系统 —— 后端服务启动")
    logger.info("=" * 66)

    init_db()
    from app.services import automation_service
    automation_service.reset_sequence()
    _wire_mqtt_handlers()
    mqtt_client.start()          # 连不上只打警告, 服务照常启动
    start_scheduler()

    logger.info("接口文档: http://%s:%d/docs", settings.app_host, settings.app_port)
    logger.info("-" * 66)

    yield

    logger.info("正在关闭后端服务 ...")
    stop_scheduler()
    mqtt_client.stop()
    logger.info("后端服务已停止")


app = FastAPI(
    title="植物养护系统 API",
    description="基于大模型的植物养护系统 —— 后端服务(边缘端/前端之间的桥梁)",
    version="0.1.0",
    lifespan=lifespan,
)

# 前端跑在 5173 端口, 和后端不同源, 必须放行跨域
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# REST 路由统一挂到 /api 下
app.include_router(routes_health.router, prefix="/api", tags=["health"])
app.include_router(routes_device.router, prefix="/api")
app.include_router(routes_telemetry.router, prefix="/api")
app.include_router(routes_vision.router, prefix="/api")

# WebSocket 单独挂在根路径: ws://127.0.0.1:8000/ws
app.include_router(ws.router, tags=["websocket"])

# 抓拍图片的静态访问: http://127.0.0.1:8000/images/xxx.jpg
app.mount("/images", StaticFiles(directory=str(settings.image_dir)), name="images")


if __name__ == "__main__":
    # 方便直接 python app/main.py 启动(不用敲 uvicorn 命令)
    import uvicorn

    uvicorn.run(
        "app.main:app",
        host=settings.app_host,
        port=settings.app_port,
        reload=True,
    )
