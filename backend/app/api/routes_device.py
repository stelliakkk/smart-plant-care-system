# -*- coding: utf-8 -*-
"""
设备接口
---------------------------------------------------------------------
GET  /api/device/status   查询边缘端在线状态
POST /api/device/command  下发执行器指令(前端手动控制走这里)
"""
import logging

from fastapi import APIRouter, Query, HTTPException

from app.schemas.device import CommandRequest, DeviceStatus
from app.services import device_service, command_service

logger = logging.getLogger(__name__)

router = APIRouter(prefix="/device", tags=["device"])


@router.get("/status", response_model=DeviceStatus, summary="查询边缘端在线状态")
def get_status(device_id: str | None = None) -> DeviceStatus:
    return device_service.get_status(device_id)


@router.post("/command", summary="下发执行器指令")
def send_command(cmd: CommandRequest, device_id: str | None = None) -> dict:
    """
    下发指令。

    返回 sent=true 只代表报文发出去了。边缘端可能因为本地安全策略
    (例如两泵互锁或指令过期)而拒绝执行, 那种情况要看 cmd_ack 的 reason。
    """
    logger.info("收到手动指令: %s", cmd.model_dump(mode="json"))
    return command_service.submit(cmd, device_id)


@router.get("/control-log", summary="执行器指令与板子回执")
def control_log(limit: int = Query(30, ge=1, le=100)):
    return {"items": command_service.history(limit)}


@router.post("/emergency-stop", summary="向四路输出发送关闭指令")
def emergency_stop():
    with command_service._lock:
        for name in automation_service.MODE_KEYS: automation_service.pause_manual(name)
        results = [command_service.submit(CommandRequest(actuator=name, action="off"))
                   for name in ("pump", "fertilizer", "spray", "grow_light")]
    return {"sent": all(result["sent"] for result in results), "commands": results}



from typing import Literal
from pydantic import BaseModel, StrictBool, StrictInt, Field
from app.services import automation_service

class ModeRequest(BaseModel):
    actuator: Literal['spray', 'grow_light', 'pump', 'fertilizer']
    enabled: StrictBool

@router.get('/automation')
def automation_status():
    return automation_service.status()

@router.post('/automation')
def automation_mode(request: ModeRequest):
    try: return automation_service.configure(request.actuator, request.enabled)
    except ValueError as exc: raise HTTPException(400,str(exc)) from None

class PumpScheduleRequest(BaseModel):
    actuator: Literal['pump','fertilizer']
    interval_sec: StrictInt = Field(ge=60,le=604800)
    duration_ms: StrictInt = Field(ge=1,le=30000)

@router.post('/pump-schedule')
def pump_schedule(request: PumpScheduleRequest):
    return automation_service.configure_pump(request.actuator,request.interval_sec,request.duration_ms)
