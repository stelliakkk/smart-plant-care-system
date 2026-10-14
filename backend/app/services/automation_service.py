"""Single-worker automation. All policy/command transactions share the command lock."""
import json
from datetime import datetime, timedelta
from app.config import settings
from app.db.models import AutomationState, ActuatorCommand
from app.db.session import SessionLocal
from app.schemas.device import CommandRequest
from app.services import command_service as cs, device_service as ds

COOLDOWN = timedelta(hours=48)

def _load(db):
    row = db.get(AutomationState, settings.device_id)
    if row is None:
        row = AutomationState(device_id=settings.device_id, data='{}')
        db.add(row)
    state = dict(light_auto=False, spray_auto=False, count=0, last_record=0,
                 last_photo='', eligible_after='', cooldown_until='', last_command='', last_reason='尚未开启自动模式', events=[])
    state.update(json.loads(row.data))
    return row, state

def _save(db, row, state):
    row.data = json.dumps(state, ensure_ascii=False)
    db.commit()

def _event(state, reason, record_id=None):
    state['last_reason'] = reason
    state['events'] = [dict(ts=datetime.now().isoformat(), reason=reason, record_id=record_id)] + state['events'][:99]

def status():
    with cs._lock, SessionLocal() as db:
        row, state = _load(db)
        _save(db, row, state)
        until = datetime.fromisoformat(state['cooldown_until']) if state['cooldown_until'] else datetime.min
        state['cooldown_remaining_sec'] = max(0, int((until-datetime.now()).total_seconds()))
        board = ds.get_status()
        state['board_modes'] = board.automation
        state['automation_ready'] = board.automation_ready and board.online
        state['light_lux'] = board.light_lux
        state['light_fault'] = board.light_fault
        command=db.get(ActuatorCommand,state['last_command']) if state['last_command'] else None
        state['spray_command_status'] = cs.serialize(command) if command else None
        state['thresholds'] = dict(on_lux=3000, on_sec=10, off_lux=4000, off_sec=30)
        return state

def configure(actuator, enabled):
    with cs._lock, SessionLocal() as db:
        row, state = _load(db)
        state['light_auto' if actuator == 'grow_light' else 'spray_auto'] = enabled
        if actuator == 'spray':
            state['count'] = 0
            state['last_photo'] = ''
            state['eligible_after'] = datetime.now().isoformat()
        _event(state, f'{actuator} 自动模式'+('开启，等待设备确认' if enabled else '暂停'))
        _save(db, row, state)
        reconcile()
        return status()

def pause_manual(actuator):
    if actuator not in ('grow_light', 'spray'): return
    with cs._lock, SessionLocal() as db:
        row, state = _load(db)
        state['light_auto' if actuator == 'grow_light' else 'spray_auto'] = False
        if actuator == 'spray':
            state['count'] = 0
            state['eligible_after'] = datetime.now().isoformat()
        _event(state, f'{actuator} 手动操作，暂停该路自动模式')
        _save(db, row, state)

def reconcile():
    """Heartbeat reconciliation retries unconfirmed policy changes; never retries spraying."""
    with cs._lock:
        board = ds.get_status()
        if not board.online or not board.automation_ready: return
        state = status()
        for actuator, key in [('grow_light','light_auto'), ('spray','spray_auto')]:
            desired = state[key]
            if board.automation.get(actuator) is desired: continue
            recent = cs.history(30)
            if any(r['actuator']==actuator and r['action'] in ('auto_on','auto_off') and
                   r['result']=='pending' for r in recent): continue
            cs.submit(CommandRequest(actuator=actuator, action='auto_on' if desired else 'auto_off'), internal=True)

def reserve_spray(command_id, source="manual"):
    with SessionLocal() as db:
        row, state = _load(db)
        state['reservation_previous_until'] = state['cooldown_until']
        state.update(last_command=command_id, cooldown_until=(datetime.now()+COOLDOWN).isoformat(), count=0)
        _event(state, '喷洒指令待确认，保守冷却 48 小时', state['last_record'] if source=='auto' else None)
        state['events'][0].update(command_id=command_id,source=source)
        _save(db, row, state)

def spray_ack(command_id, result, reason):
    with SessionLocal() as db:
        row, state = _load(db)
        if state['last_command'] != command_id: return
        if result == 'running':
            state['cooldown_until'] = (datetime.now()+COOLDOWN).isoformat()
            _event(state, 'S3 确认输出开启，冷却 48 小时')
        elif result == 'rejected' or reason in ('MQTT 指令未发出','output_start_failed'):
            state['cooldown_until'] = state.get('reservation_previous_until','')
            _event(state, '喷洒未执行，重新累计检测')
        elif result == 'done': _event(state, 'S3 确认输出关闭，继续冷却')
        else: _event(state, '喷洒执行情况未知或异常，保留冷却')
        _save(db, row, state)

def evaluate(record):
    if record.get('source') != 'scheduled': return
    with cs._lock, SessionLocal() as db:
        row, state = _load(db)
        if record['id'] <= state['last_record']: return
        state['last_record'] = record['id']
        now = datetime.now()
        until = datetime.fromisoformat(state['cooldown_until']) if state['cooldown_until'] else datetime.min
        result = record.get('result') or {}
        photo_time = record['ts']
        if isinstance(photo_time,str): photo_time = datetime.fromisoformat(photo_time)
        last = datetime.fromisoformat(state['last_photo']) if state['last_photo'] else None
        # No old, interrupted or long-gap analyses may form a consecutive pair.
        if last and (photo_time-last).total_seconds() > max(360, settings.vision_interval_sec+60): state['count']=0
        valid = (record.get('status')=='done' and result.get('pest_status')=='detected' and
                 bool(result.get('pest_evidence','').strip()) and (now-photo_time).total_seconds() <= 180)
        board = ds.get_status()
        reason = ''
        if not state['spray_auto']: reason='自动喷洒暂停'
        elif now < until: reason='冷却中，未喷洒'
        elif photo_time < until or (state['eligible_after'] and photo_time <= datetime.fromisoformat(state['eligible_after'])): reason='旧照片不计入当前检测周期'
        elif not valid: reason='无明确虫害或分析无效，连续检测清零'
        elif not board.online or not board.automation_ready or not board.automation.get('spray'): reason='S3 离线或自动模式尚未确认'
        elif board.actuators.get('spray') is not False: reason='农药泵状态未知或正在运行'
        if reason:
            state['count']=0; state['last_photo']=''
        else:
            state['count'] += 1; state['last_photo']=photo_time.isoformat()
            reason=f'明确虫害，连续检测 {state["count"]}/2'
        _event(state, reason, record['id'])
        trigger = state['count'] >= 2
        if trigger: state['count']=0; state['last_photo']=''
        _save(db, row, state)
        if trigger:
            response = cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=2000,source='auto'), internal=True)
            if not response['sent']:
                row, state = _load(db)
                _event(state, response.get('reason','指令未发出'),record['id']); _save(db,row,state)

def reset_sequence():
    with cs._lock, SessionLocal() as db:
        row,state=_load(db); state['count']=0;state['last_photo']='';state['eligible_after']=datetime.now().isoformat();_save(db,row,state)

def observe_light(data):
    """Journal changed board reports, without claiming measured lamp output."""
    modes=data.get('automation') or {}
    outputs=data.get('actuators') or {}
    value=outputs.get('grow_light')
    if not isinstance(value,bool): return
    with cs._lock, SessionLocal() as db:
        row,state=_load(db)
        previous=state.get('light_report')
        state['light_report']=value
        fault=data.get('light_fault','')
        if modes.get('grow_light') is True and (previous is not value or fault!=state.get('light_report_fault','')):
            reason='S3 报告自动补光输出'+('开启' if value else '关闭')
            if fault: reason+='：'+fault
            else: reason+=f'，光照 {data.get("light_lux")} lux'
            _event(state,reason)
        state['light_report_fault']=fault
        _save(db,row,state)
