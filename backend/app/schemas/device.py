# -*- coding: utf-8 -*-
"""
设备与指令契约
---------------------------------------------------------------------
CommandRequest 就是"边缘端能听懂的指令格式"。
注意: 这里只下发"意图"(开哪个执行器、打算开多久),
      真正能不能执行、实际执行多久, 由边缘端的 plant_ctrl 组件决定 ——
      安全策略必须在本地, 不能依赖网络在线的后端。
"""
from datetime import datetime
from enum import Enum
from typing import Literal

from pydantic import BaseModel, Field, StrictInt


class ActuatorName(str, Enum):
    """执行器枚举。值同时也是 MQTT 报文里的字符串。"""

    PUMP = "pump"                 # 补水泵
    FERTILIZER = "fertilizer"     # 施肥泵(高浓度盐水)
    GROW_LIGHT = "grow_light"     # 补光灯
    SPRAY = "spray"               # 农药喷雾


class CommandRequest(BaseModel):
    """后端 -> 边缘端 的执行器指令。"""

    actuator: ActuatorName
    action: Literal["on", "off", "auto_on", "auto_off"]
    duration_ms: StrictInt = Field(
        default=0,
        ge=0,
        le=600_000,
        description="计划运行时长(毫秒), 仅 action=on 时有意义",
    )
    source: Literal["manual", "auto", "ai"] = Field(
        default="manual",
        description="下令来源: manual=前端手动, auto=阈值闭环, ai=AI 诊断触发",
    )


class CommandAck(BaseModel):
    """边缘端 -> 后端 的执行回执。"""

    command_id: str = Field(pattern="^[0-9a-f]{32}$")
    boot_id: str = Field(pattern="^[0-9a-f]{8}$")
    actuator: ActuatorName
    result: Literal["running", "done", "rejected", "failed"]
    reason: str = ""
    ts: datetime | None = None


class DeviceStatus(BaseModel):
    """边缘端心跳状态。"""

    device_id: str = "edge01"
    online: bool = False
    rssi: int | None = Field(default=None, description="WiFi 信号强度 dBm")
    uptime_s: int | None = Field(default=None, description="上电运行秒数")
    last_seen: datetime | None = None
    fault: str = ""
    boot_id: str = ""
    control_ready: bool = False
    actuators: dict[str, bool] = Field(default_factory=dict)
    automation_ready: bool = False
    automation: dict[str, bool] = Field(default_factory=dict)
    light_lux: float | None = None
    light_fault: str = ""

