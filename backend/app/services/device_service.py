# -*- coding: utf-8 -*-
"""
设备服务
---------------------------------------------------------------------
负责: 边缘端的在线状态维护、指令下发。

"在线"的判定方式:
    边缘端定期上报心跳, 后端记下最后一次收到的时间。
    查询时如果发现超过 OFFLINE_TIMEOUT_SEC 没动静, 就认为离线。

⚠️ 重要设计原则:
    后端显示"在线/离线"只是为了给用户看, 不能把它当成控制逻辑的前提。
    边缘端自己才是安全的最终负责人 —— 断网时它必须能独立按本地规则运行。
"""
import logging
from datetime import datetime, timedelta

from app.config import settings
from app.mqtt import topics
from app.mqtt.client import mqtt_client
from app.schemas.device import CommandRequest, DeviceStatus

logger = logging.getLogger(__name__)

# 多久没收到任何上行报文, 就判定为离线(秒)
OFFLINE_TIMEOUT_SEC = 30

# 设备状态先放内存(阶段 1 接上数据库后再持久化)
_status_cache: dict[str, DeviceStatus] = {}
_uptime_seen: dict[str, datetime] = {}


def mark_seen(
    device_id: str,
    rssi: int | None = None,
    uptime_s: int | None = None,
    fault: str = "",
    boot_id: str | None = None,
    control_ready: bool | None = None,
    actuators: dict | None = None,
    automation: dict | None = None,
    automation_ready: bool = False,
    light_lux: float | None = None,
    light_fault: str = "",
) -> DeviceStatus:
    """收到任何一条上行报文时调用, 刷新在线状态。"""
    now = datetime.now()
    status = _status_cache.get(device_id) or DeviceStatus(device_id=device_id)
    status.online = True
    status.last_seen = now
    if rssi is not None:
        status.rssi = rssi
    if uptime_s is not None:
        status.uptime_s = uptime_s
        _uptime_seen[device_id] = now
    if boot_id is not None:
        if status.boot_id != boot_id:
            status.automation_ready=False
            status.automation={}
            status.actuators={}
        status.boot_id = boot_id if isinstance(boot_id, str) else ""
    if control_ready is not None:
        status.control_ready = control_ready is True
    if isinstance(actuators, dict):
        status.actuators = {key: value for key, value in actuators.items()
                            if key in {"pump", "fertilizer", "spray", "grow_light"} and isinstance(value, bool)}
    if automation is not None:
        status.automation = {k:v for k,v in automation.items() if k in {"spray","grow_light"} and isinstance(v,bool)}
        status.automation_ready = automation_ready is True
        status.light_lux = light_lux
        status.light_fault = light_fault
    status.fault = fault
    _status_cache[device_id] = status
    return status


def get_status(device_id: str | None = None) -> DeviceStatus:
    """查询设备状态(超时未上报自动判为离线)。"""
    device_id = device_id or settings.device_id
    status = _status_cache.get(device_id)
    if status is None:
        return DeviceStatus(device_id=device_id, online=False)
    if status.last_seen and datetime.now() - status.last_seen > timedelta(
        seconds=OFFLINE_TIMEOUT_SEC
    ):
        status.online = False
    return status


def send_command(cmd: CommandRequest, device_id: str | None = None) -> bool:
    """Compatibility entry point; all callers share correlated command checks."""
    from app.services import command_service
    return command_service.submit(cmd, device_id)["sent"]
