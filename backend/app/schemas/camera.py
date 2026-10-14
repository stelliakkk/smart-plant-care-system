# -*- coding: utf-8 -*-
"""ESP32-CAM 实时流与帧元数据的 API 契约。"""
from datetime import datetime

from pydantic import BaseModel


class CameraFrameInfo(BaseModel):
    id: int | None = None
    device_id: str = "cam01"
    ts: datetime | None = None
    content_type: str = ""
    byte_size: int = 0
    captured: bool = False
    stored: bool = False
    error: str = ""


class CameraStatus(BaseModel):
    device_id: str = "cam01"
    online: bool = False
    stream_available: bool = False
    last_seen: datetime | None = None
    rssi: int | None = None
    fault: str = ""
    latest_frame: CameraFrameInfo | None = None
