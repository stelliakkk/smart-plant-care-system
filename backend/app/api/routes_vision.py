"""Camera discovery, stored photograph history, and asynchronous analysis APIs."""
from fastapi import APIRouter, HTTPException, Query
from app.services import vision_service, camera_service, device_service
from app.api.deps import DbSession
from app.config import settings

router = APIRouter(prefix='/vision', tags=['vision'])

@router.get('/camera/status')
def camera_status(db: DbSession):
    status = device_service.get_status(settings.camera_device_id)
    endpoint = camera_service.endpoint()
    return dict(device_id=settings.camera_device_id, online=status.online,
                stream_available=False, capture_available=bool(endpoint.snapshot_url),
                analysis_busy=vision_service.busy(), ai_configured=bool(settings.deepseek_api_key.strip()),
                capture_interval_sec=settings.vision_interval_sec, retention_days=settings.vision_retention_days,
                last_seen=status.last_seen, rssi=status.rssi, fault=status.fault,
                latest_frame=camera_service.latest_frame(db))

@router.get('/live')
def live_stream():
    if not device_service.get_status(settings.camera_device_id).online:
        raise HTTPException(503, '相机离线或尚未公布拍照地址')
    raise HTTPException(410, '实时预览已取消，请查看最新照片或立即抓拍')

@router.get('/latest')
def get_latest():
    records = vision_service.history(limit=1)
    return dict(available=bool(records), data=records[0] if records else None, busy=vision_service.busy())

@router.get('/history')
def history(limit: int = Query(30, ge=1, le=100), before_id: int | None = Query(None, ge=1)):
    records = vision_service.history(limit, before_id)
    return dict(items=records, next_cursor=records[-1]['id'] if len(records) == limit else None)

@router.post('/analyze', status_code=202)
def analyze_now():
    if not device_service.get_status(settings.camera_device_id).online:
        raise HTTPException(503, '相机离线，请检查 CAM 的 Wi-Fi 和 MQTT')
    if not vision_service.submit():
        raise HTTPException(409, '已有拍照分析任务正在执行，请稍候')
    return dict(accepted=True, analyzed=False, message='已开始拍照，结果将自动更新')

@router.get('/record/{record_id}')
def record_by_id(record_id: int):
    record=vision_service.get_record(record_id)
    if record is None: raise HTTPException(404,'照片不存在或已过期')
    return record
