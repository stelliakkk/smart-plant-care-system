# -*- coding: utf-8 -*-
"""
健康检查接口
---------------------------------------------------------------------
前端首页那个"后端已连通"的绿点就是调这个接口来的,
同时也顺便告诉前端 MQTT 和 DeepSeek 有没有配好。
"""
from fastapi import APIRouter

from app.config import settings
from app.mqtt.client import mqtt_client

router = APIRouter()


@router.get("/health", summary="服务健康检查")
def health() -> dict:
    return {
        "status": "ok",
        "app": settings.app_name,
        "device_id": settings.device_id,
        "mqtt_connected": mqtt_client.connected,
        "deepseek_configured": bool(settings.deepseek_api_key),
        "vision_interval_sec": settings.vision_interval_sec,
    }
