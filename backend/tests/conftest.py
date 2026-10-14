# -*- coding: utf-8 -*-
"""
pytest 全局夹具 —— 测试环境隔离
=====================================================================
★ 本文件存在的唯一理由, 是修掉一个真实踩过的坑(2026-09-13):

    pytest 里的 TestClient(app) 会触发 FastAPI 的 lifespan 启动钩子,
    于是测试进程也去连 MQTT broker —— 用的还是 .env 里那个
    MQTT_CLIENT_ID=plant-care-backend。

    而 MQTT 规范规定: broker 收到"相同 client_id"的新连接时, 必须
    踢掉旧的连接。结果就是: 一跑测试, 正在运行的正式后端就被顶下线了。
    更麻烦的是 broker 踢人时不一定发 DISCONNECT 报文, paho 察觉不到,
    于是 connected 一直是 True, 永远不会自动重连 ——
    现象是"网页显示设备离线, 但板子和 broker 都好好的"。

两道防线(两条都要, 缺一不可):
    1) MQTT_DISABLED=1       测试进程压根不连 broker, 从源头断掉冲突
    2) client_id 加 -pytest  万一有人漏了第 1 条, 也撞不上正式后端

⚠ 时机很关键: 环境变量必须在 app.config 被 import 之前设好。
   pytest 保证"先执行 conftest.py, 再收集(import)测试模块",
   所以把设置放在文件最顶部(任何 app.* 的 import 之前)就正好。
   pydantic-settings 的优先级是"真实环境变量 > .env 文件",
   所以这里设的值一定能覆盖 .env 里的同名项。
=====================================================================
"""
import os
import tempfile
from pathlib import Path

# ---- 必须在任何 app.* 被 import 之前执行, 不要往下挪 ----
os.environ["MQTT_DISABLED"] = "1"
os.environ["MQTT_CLIENT_ID"] = "plant-care-backend-pytest"
_test_storage = tempfile.TemporaryDirectory(prefix="plant-care-tests-")
os.environ["DATABASE_URL"] = "sqlite:///" + (Path(_test_storage.name) / "test.db").as_posix()
os.environ["IMAGE_STORAGE_DIR"] = str(Path(_test_storage.name) / "images")
os.environ["VISION_INTERVAL_SEC"] = "0"
os.environ["DEEPSEEK_API_KEY"] = ""

import pytest  # noqa: E402  (故意放在环境变量之后, 用位置强调顺序)


@pytest.fixture(scope="session", autouse=True)
def _assert_isolated_env():
    """
    兜底断言: 万一哪天有人把上面的环境变量挪走了/写错了, 这里会立刻
    报错并说明原因 —— 而不是让"跑完测试后端就失联"这种玄学问题再发生一次。

    这就是给今天这个 bug 加的"回归护栏"。
    """
    from app.config import settings

    assert settings.mqtt_disabled is True, (
        "测试环境必须禁用 MQTT 连接(否则会顶掉正在运行的正式后端), "
        "见 tests/conftest.py 顶部说明"
    )
    assert settings.mqtt_client_id.endswith("-pytest"), (
        f"测试用的 client_id 必须带 -pytest 后缀, 当前为 {settings.mqtt_client_id!r}"
    )
    yield
    from app.db.session import engine
    engine.dispose()
    _test_storage.cleanup()
