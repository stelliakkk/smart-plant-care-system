# -*- coding: utf-8 -*-
"""ESP32-CAM 服务发现、实时流地址与无图像持久化的抓帧记录。"""
import logging
import threading
from dataclasses import dataclass
from urllib.parse import urlparse

import httpx
from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import settings
from app.db.models import CameraFrame
from app.schemas.camera import CameraFrameInfo

logger = logging.getLogger(__name__)


@dataclass
class CameraEndpoint:
    stream_url: str = ""
    snapshot_url: str = ""


_endpoint = CameraEndpoint()
_lock = threading.Lock()


def _valid_http_url(value: object) -> str:
    if not isinstance(value, str):
        return ""
    parsed = urlparse(value)
    return value if parsed.scheme == "http" and parsed.netloc else ""


def update_from_status(payload: dict) -> None:
    """只接受 cam01 的 HTTP 地址；相机重连热点后可覆盖旧地址。"""
    if payload.get("device_id") != settings.camera_device_id:
        return
    stream_url = _valid_http_url(payload.get("stream_url"))
    snapshot_url = _valid_http_url(payload.get("snapshot_url"))
    with _lock:
        if "stream_url" in payload:
            _endpoint.stream_url = stream_url
        if snapshot_url:
            _endpoint.snapshot_url = snapshot_url


def endpoint() -> CameraEndpoint:
    with _lock:
        return CameraEndpoint(_endpoint.stream_url, _endpoint.snapshot_url)


def latest_frame(db: Session) -> CameraFrameInfo | None:
    row = db.scalar(
        select(CameraFrame)
        .where(CameraFrame.device_id == settings.camera_device_id)
        .order_by(CameraFrame.ts.desc())
        .limit(1)
    )
    if row is None:
        return None
    return CameraFrameInfo(
        id=row.id,
        device_id=row.device_id,
        ts=row.ts,
        content_type=row.content_type,
        byte_size=row.byte_size,
        captured=row.captured,
        stored=row.stored,
        error=row.error,
    )


def capture_metadata() -> CameraFrame:
    """请求单帧、记元数据、立即丢弃 JPEG；无论成功失败都留可追溯记录。"""
    current = endpoint()
    row = CameraFrame(device_id=settings.camera_device_id, source_url=current.snapshot_url)
    if not current.snapshot_url:
        row.error = "相机尚未公布 snapshot_url"
        _save(row)
        return row

    try:
        response = httpx.get(current.snapshot_url, timeout=settings.camera_http_timeout_sec)
        response.raise_for_status()
        row.content_type = response.headers.get("content-type", "").split(";", 1)[0]
        if row.content_type != "image/jpeg":
            row.error = f"相机返回非 JPEG 内容: {row.content_type or 'unknown'}"
        else:
            row.byte_size = len(response.content)
            row.captured = True
        # response.content 仅用于计算长度；函数返回后没有任何图片引用或落盘动作。
    except Exception as exc:
        row.error = f"抓帧失败: {exc}"
        logger.warning("ESP32-CAM 抓帧失败: %s", exc)
    _save(row)
    return row


def _save(row: CameraFrame) -> None:
    from app.db.session import SessionLocal

    with SessionLocal() as db:
        db.add(row)
        db.commit()
        db.refresh(row)
