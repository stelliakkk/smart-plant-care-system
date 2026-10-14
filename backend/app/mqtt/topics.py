# -*- coding: utf-8 -*-
"""
MQTT 主题定义
---------------------------------------------------------------------
主题即"接口", 一旦边缘端固件写死就很难改, 所以集中在这一个文件里管理。

统一格式:  plant/{device_id}/{功能}

  telemetry   边缘端 -> 后端   传感器数据上报
  status      边缘端 -> 后端   心跳/在线状态
  cmd         后端   -> 边缘端 执行器指令
  cmd_ack     边缘端 -> 后端   执行回执(含被安全策略拒绝的原因)
  vision      后端内部              图像分析结果(可推给前端)
"""
from app.config import settings

TOPIC_ROOT = "plant"


def topic_telemetry(device_id: str) -> str:
    return f"{TOPIC_ROOT}/{device_id}/telemetry"


def topic_status(device_id: str) -> str:
    return f"{TOPIC_ROOT}/{device_id}/status"


def topic_command(device_id: str) -> str:
    return f"{TOPIC_ROOT}/{device_id}/cmd"


def topic_command_ack(device_id: str) -> str:
    return f"{TOPIC_ROOT}/{device_id}/cmd_ack"


def topic_vision(device_id: str) -> str:
    return f"{TOPIC_ROOT}/{device_id}/vision"


def subscribe_pattern(device_id: str) -> str:
    """订阅该设备的全部上行主题。"""
    return f"{TOPIC_ROOT}/{device_id}/#"


# 当前默认设备(单设备演示够用, 将来多设备就从数据库取)
DEFAULT_DEVICE_ID = settings.device_id
