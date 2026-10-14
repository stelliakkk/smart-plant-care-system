"""Correlated MQTT commands, acknowledgements and honest execution status."""
import threading
import uuid
from datetime import datetime, timedelta
from sqlalchemy import select
from app.config import settings
from app.db.models import ActuatorCommand
from app.db.session import SessionLocal
from app.mqtt import topics
from app.mqtt.client import mqtt_client
from app.services import device_service

_lock = threading.RLock()
TERMINAL = {'done','rejected','failed','timeout'}

def serialize(row):
    return {key:getattr(row,key) for key in ('command_id','device_id','ts','updated_at','actuator','action','duration_ms','result','reason')}

def _expire(db):
    now = datetime.now()
    expired_sprays=[]
    for row in db.scalars(select(ActuatorCommand).where(ActuatorCommand.result.in_(['pending','running']))):
        status = device_service.get_status(row.device_id)
        if not status.online or status.boot_id != row.boot_id:
            row.result='timeout'; row.reason='设备离线或已重启，旧指令实际状态未知'
            if row.actuator=='spray' and row.action=='on': expired_sprays.append((row.command_id,row.reason))
            continue
        wait = 15 if row.result == 'pending' else row.duration_ms / 1000 + 15
        if row.result == 'running' and row.duration_ms == 0:
            # Continuous light output has no completion deadline; status checks remain available.
            continue
        if (now-row.updated_at).total_seconds() > wait:
            row.result='timeout'
            row.reason='未收到板子确认，实际输出状态未知，请检查设备或发送关闭指令'
            if row.actuator=='spray' and row.action=='on': expired_sprays.append((row.command_id,row.reason))
    db.commit()
    from app.services import automation_service
    for command_id,reason in expired_sprays: automation_service.spray_ack(command_id,'timeout',reason)

def submit(cmd, device_id=None, *, internal=False):
    with _lock:
        return _submit(cmd, device_id, internal=internal)

def _submit(cmd, device_id=None, *, internal=False):
    from app.services import automation_service as auto
    if not internal and cmd.source == "manual" and cmd.action in ("on","off") and (device_id is None or device_id == settings.device_id):
        auto.pause_manual(cmd.actuator.value)
    device_id = device_id or settings.device_id
    status = device_service.get_status(device_id)
    reason = ''
    if device_id != settings.device_id: reason='只允许控制配置的 S3 设备'
    elif cmd.source != 'manual' and not internal: reason='自动指令仅允许由后端规则生成'
    elif cmd.action in ('auto_on','auto_off') and not internal: reason='请使用自动模式接口'
    elif internal and not status.automation_ready: reason='请先更新 S3 自动控制固件'
    elif not status.online or not mqtt_client.connected: reason='S3 或 MQTT 离线'
    elif not status.control_ready or not status.boot_id: reason='当前 S3 固件未启用网页控制，请先烧录新版固件'
    elif cmd.action == 'on' and cmd.actuator.value != 'grow_light' and not 1 <= cmd.duration_ms <= 30000:
        reason='泵运行时长必须为 1～30000 毫秒'
    if not reason and internal and cmd.source != 'manual':
        policy=auto.status()
        if (cmd.actuator.value!='spray' or cmd.action!='on' or cmd.duration_ms!=2000 or
            not policy['spray_auto'] or not status.automation.get('spray') or
            policy['cooldown_remaining_sec']>0 or status.actuators.get('spray') is not False):
            reason='自动喷洒模式、时长、冷却或输出状态不满足条件'
    if reason: return dict(sent=False,reason=reason,command=cmd.model_dump(mode='json'),device_id=device_id)
    duration = cmd.duration_ms if cmd.action == 'on' else 0
    command_id=uuid.uuid4().hex
    seen = device_service._uptime_seen.get(device_id, datetime.now())
    uptime_ms=int(((status.uptime_s or 0)+(datetime.now()-seen).total_seconds())*1000)
    payload=dict(command_id=command_id,boot_id=status.boot_id,actuator=cmd.actuator.value,
                 action=cmd.action,duration_ms=duration,source=cmd.source,deadline_uptime_ms=uptime_ms+15000)
    # Commit before publishing: a fast board ACK must always find its record.
    with _lock, SessionLocal() as db:
        row=ActuatorCommand(command_id=command_id,device_id=device_id,boot_id=status.boot_id,
                            actuator=cmd.actuator.value,action=cmd.action,duration_ms=duration)
        db.add(row); db.commit()
        if cmd.actuator.value == "spray" and cmd.action == "on":
            auto.reserve_spray(command_id,cmd.source)
        try: sent=mqtt_client.publish_json(topics.topic_command(device_id),payload,qos=1)
        except Exception: sent=False
        if not sent:
            row.result='failed'; row.reason='MQTT 指令未发出'; db.commit()
            if row.actuator == 'spray' and row.action == 'on': auto.spray_ack(command_id,'failed',row.reason)
        return dict(sent=sent,command_id=command_id,record=serialize(row),device_id=device_id,
                    command=cmd.model_dump(mode='json'),reason=row.reason,
                    note='等待 S3 回执；输出设置成功不等同于测量到负载运转')

def acknowledge(topic, data):
    if not isinstance(data,dict): return
    if topic != topics.topic_command_ack(settings.device_id): return
    command_id=data.get('command_id')
    result=data.get('result')
    if not isinstance(command_id,str) or not isinstance(result,str) or result not in {'running','done','rejected','failed'}: return
    with _lock, SessionLocal() as db:
        row=db.get(ActuatorCommand,command_id)
        if not row or data.get('boot_id') != row.boot_id or data.get('actuator') != row.actuator: return
        if row.result in TERMINAL and not (row.result == 'timeout' and result in TERMINAL): return
        if result == 'running' and row.action != 'on': return
        if row.result == 'running' and result in {'running','rejected'}: return
        row.result=result; row.updated_at=datetime.now()
        reason=data.get('reason','')
        row.reason=reason[:200] if isinstance(reason,str) else ''
        db.commit()
        device_service.mark_seen(row.device_id)
        if row.actuator == "spray" and row.action == "on":
            from app.services import automation_service
            automation_service.spray_ack(command_id,result,row.reason)

def history(limit=30):
    with _lock, SessionLocal() as db:
        _expire(db)
        return [serialize(row) for row in db.scalars(select(ActuatorCommand).where(
            ActuatorCommand.device_id==settings.device_id).order_by(ActuatorCommand.ts.desc()).limit(limit))]
