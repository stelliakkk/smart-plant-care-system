# -*- coding: utf-8 -*-
"""
传感器数据契约
---------------------------------------------------------------------
TelemetryPayload 同时也是"边缘端上报格式"的权威定义:
ESP32 通过 MQTT 发上来的 JSON 必须能被它解析, 详见 docs/mqtt_topics.md
"""
from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field


class TelemetryPayload(BaseModel):
    """边缘端上报的一组传感器数据。"""

    device_id: str = Field(default="edge01", description="设备编号")
    ts: datetime | None = Field(default=None, description="采集时间, 缺省则用服务器时间")

    soil_moisture: float | None = Field(default=None, description="土壤湿度 %")
    soil_ec: float | None = Field(default=None, description="土壤盐分 mS/cm")
    temperature: float | None = Field(default=None, description="环境温度 °C")
    light_lux: float | None = Field(default=None, description="光照强度 lux")


class SensorReadingOut(TelemetryPayload):
    """返回给前端的读数(多了数据库自增 id)。"""

    model_config = ConfigDict(from_attributes=True)

    id: int
