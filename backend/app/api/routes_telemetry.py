# -*- coding: utf-8 -*-
"""
传感器数据接口
---------------------------------------------------------------------
GET  /api/telemetry/latest    最新一条读数(总览页)
GET  /api/telemetry/history   历史读数(曲线页)
POST /api/telemetry/simulate  手动塞一条假数据 —— 调试前端时非常好用,
                              不用等真实硬件上报
"""
import logging

from fastapi import APIRouter

from app.schemas.telemetry import SensorReadingOut, TelemetryPayload
from app.services import telemetry_service

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/telemetry", tags=["telemetry"])


@router.get("/latest", summary="最新一条传感器读数")
def get_latest(device_id: str | None = None) -> dict:
    row = telemetry_service.get_latest(device_id)
    if row is None:
        return {"available": False, "data": None}
    return {
        "available": True,
        "data": SensorReadingOut.model_validate(row).model_dump(mode="json"),
    }


@router.get("/history", summary="历史传感器读数")
def get_history(device_id: str | None = None, limit: int = 200) -> dict:
    rows = telemetry_service.get_history(device_id, limit=limit)
    return {
        "count": len(rows),
        "data": [
            SensorReadingOut.model_validate(r).model_dump(mode="json") for r in rows
        ],
    }


@router.post("/simulate", summary="手动写入一条模拟数据(调试用)")
def simulate(payload: TelemetryPayload) -> dict:
    """把一条数据按真实流程入库, 方便在没有硬件时联调前端。"""
    new_id = telemetry_service.save_reading(payload)
    # TODO(阶段 2): 这里之后还要顺手做阈值判断 + WebSocket 推送给前端
    return {"saved": True, "id": new_id}
