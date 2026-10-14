"""Policy regressions with isolated SQLite and mocked MQTT; no physical outputs."""
from datetime import datetime, timedelta
import pytest
from sqlalchemy import delete
from fastapi.testclient import TestClient
from app.main import app
from app.db.models import AutomationState, ActuatorCommand
from app.db.session import init_db, SessionLocal
from app.config import settings
from app.services import automation_service as auto, command_service as cs, device_service as ds
from app.schemas.device import CommandRequest
from app.mqtt.client import mqtt_client
from app.mqtt import topics

@pytest.fixture(autouse=True)
def setup(monkeypatch):
    init_db()
    with SessionLocal() as db:
        db.execute(delete(AutomationState)); db.execute(delete(ActuatorCommand)); db.commit()
    ds._status_cache.clear()
    ds.mark_seen(settings.device_id,uptime_s=100,boot_id='1234abcd',control_ready=True,
                 automation_ready=True,automation={'spray':True,'grow_light':True},
                 actuators={'spray':False,'grow_light':False})
    monkeypatch.setattr(mqtt_client,'_connected',True)
    packets=[]
    monkeypatch.setattr(mqtt_client,'publish_json',lambda topic,payload,qos=0:packets.append(payload) or True)
    yield packets
    ds._status_cache.clear()

def policy(**values):
    with cs._lock,SessionLocal() as db:
        row,state=auto._load(db);state.update(values);auto._save(db,row,state)

def photo(i, pest='detected', **kwargs):
    return dict(id=i,source='scheduled',ts=datetime.now(),status='done',
                result={'pest_status':pest,'pest_evidence':'叶背清晰可见小虫'}, **kwargs)

def receipt(command,result,reason='test'):
    cs.acknowledge(topics.topic_command_ack(settings.device_id),dict(
        command_id=command['command_id'],boot_id='1234abcd',actuator='spray',result=result,reason=reason))

def test_two_photos_and_cooldown(setup):
    policy(spray_auto=True)
    auto.evaluate(photo(1));assert auto.status()['count']==1 and not setup
    auto.evaluate(photo(2));assert len(setup)==1 and setup[0]['duration_ms']==2000
    assert setup[0]['source']=='auto' and auto.status()['cooldown_remaining_sec']>172790
    for i in (3,4,5): auto.evaluate(photo(i))
    assert len(setup)==1 and auto.status()['count']==0

@pytest.mark.parametrize('pest',['none','suspected','unknown'])
def test_uncertain_breaks_sequence(setup,pest):
    policy(spray_auto=True);auto.evaluate(photo(1));auto.evaluate(photo(2,pest));auto.evaluate(photo(3))
    assert auto.status()['count']==1 and not setup

def test_failure_and_no_evidence_reset(setup):
    policy(spray_auto=True);auto.evaluate(photo(1))
    bad=photo(2);bad['status']='error';auto.evaluate(bad)
    assert auto.status()['count']==0
    bad=photo(3);bad['result']['pest_evidence']='';auto.evaluate(bad)
    assert not setup and auto.status()['count']==0

def test_manual_capture_and_duplicates_do_not_accelerate(setup):
    policy(spray_auto=True);auto.evaluate(photo(1));auto.evaluate(photo(1))
    manual=photo(2);manual['source']='manual';auto.evaluate(manual)
    assert auto.status()['count']==1 and not setup

def test_old_and_long_gap_photos(setup):
    policy(spray_auto=True);auto.evaluate(photo(1))
    policy(last_photo=(datetime.now()-timedelta(minutes=12)).isoformat())
    auto.evaluate(photo(2));assert auto.status()['count']==1
    old=photo(3);old['ts']=datetime.now()-timedelta(minutes=4);auto.evaluate(old)
    assert auto.status()['count']==0 and not setup

def test_manual_priority_and_light_independence(setup):
    policy(light_auto=True,spray_auto=True,count=1)
    cs.submit(CommandRequest(actuator='grow_light',action='off'))
    state=auto.status();assert not state['light_auto'] and state['spray_auto'] and state['count']==1
    cmd=cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))
    assert cmd['sent'] and not auto.status()['spray_auto'] and auto.status()['cooldown_remaining_sec']>0

def test_reject_clears_but_timeout_and_stop_failure_keep_cooldown(setup):
    command=cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))
    receipt(command,'rejected');assert auto.status()['cooldown_remaining_sec']==0
    command=cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))
    receipt(command,'running');receipt(command,'failed','output_stop_failed')
    assert auto.status()['cooldown_remaining_sec']>0

def test_publish_failure_clears_reservation(setup,monkeypatch):
    monkeypatch.setattr(mqtt_client,'publish_json',lambda *a,**kw:False)
    assert not cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))['sent']
    assert auto.status()['cooldown_remaining_sec']==0

def test_rejected_manual_spray_preserves_existing_cooldown(setup):
    previous=(datetime.now()+timedelta(hours=24)).isoformat()
    policy(cooldown_until=previous)
    command=cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))
    receipt(command,'rejected','actuator_busy')
    assert auto.status()['cooldown_until']==previous

def test_late_old_result_after_enable_cannot_count(setup):
    old=photo(1);old['ts']=datetime.now()-timedelta(seconds=1)
    auto.configure('spray',True)
    auto.evaluate(old)
    assert auto.status()['count']==0
    assert not any(p['actuator']=='spray' and p['action']=='on' for p in setup)

def test_concurrent_duplicate_evaluation_publishes_once(setup):
    import concurrent.futures
    policy(spray_auto=True);auto.evaluate(photo(1))
    record=photo(2)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        list(pool.map(auto.evaluate,[record]*4))
    assert len(setup)==1

def test_lost_receipt_remains_unknown_and_keeps_cooldown(setup):
    command=cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=1000))
    with SessionLocal() as db:
        row=db.get(ActuatorCommand,command['command_id'])
        row.updated_at=datetime.now()-timedelta(seconds=20);db.commit()
    assert cs.history()[0]['result']=='timeout'
    assert auto.status()['cooldown_remaining_sec']>0
    assert '未知' in auto.status()['last_reason']

def test_old_firmware_session_clears_cached_automation(setup):
    ds.mark_seen(settings.device_id,boot_id='87654321',control_ready=True)
    assert not ds.get_status().automation_ready and not ds.get_status().automation

def test_cooldown_boundary_does_not_accept_preexpiry_photo(setup):
    policy(spray_auto=True,cooldown_until=(datetime.now()-timedelta(seconds=1)).isoformat())
    old=photo(1);old['ts']=datetime.now()-timedelta(seconds=3)
    auto.evaluate(old)
    assert auto.status()['count']==0 and not setup

def test_restart_keeps_policy_and_cooldown_but_resets_count(setup):
    policy(spray_auto=True,light_auto=True,count=1,cooldown_until=(datetime.now()+timedelta(hours=48)).isoformat())
    auto.reset_sequence();state=auto.status()
    assert state['spray_auto'] and state['light_auto'] and state['count']==0 and state['cooldown_remaining_sec']>0

def test_expired_cooldown_requires_new_pair(setup):
    policy(spray_auto=True,cooldown_until=(datetime.now()-timedelta(seconds=1)).isoformat())
    auto.evaluate(photo(1));assert not setup
    auto.evaluate(photo(2));assert len(setup)==1

def test_offline_and_old_firmware_cannot_trigger(setup):
    policy(spray_auto=True);ds.get_status().automation_ready=False
    auto.evaluate(photo(1));auto.evaluate(photo(2));assert not setup

def test_external_auto_commands_blocked(setup):
    assert not cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=2000,source='auto'))['sent']
    assert not cs.submit(CommandRequest(actuator='grow_light',action='auto_on'))['sent']
    assert not setup

def test_mode_sync_dedupes_pending(setup):
    ds.get_status().automation={'spray':False,'grow_light':False}
    auto.configure('spray',True);auto.reconcile();auto.reconcile()
    assert len(setup)==1 and setup[0]['action']=='auto_on'

def test_api_modes_stop_all_and_invalid_input(setup):
    with TestClient(app) as client:
        assert client.post('/api/device/automation',json={'actuator':'pump','enabled':True}).status_code==422
        assert client.post('/api/device/automation',json={'actuator':'spray','enabled':'true'}).status_code==422
        assert client.post('/api/device/automation',json={'actuator':'spray','enabled':True}).status_code==200
        policy(light_auto=True)
        client.post('/api/device/emergency-stop')
        state=client.get('/api/device/automation').json()
        assert not state['spray_auto'] and not state['light_auto']

def test_light_reports_are_journaled_once(setup):
    report={'automation':{'grow_light':True},'actuators':{'grow_light':True},'light_lux':1000,'light_fault':''}
    auto.observe_light(report);auto.observe_light(report)
    assert len(auto.status()['events'])==1
    assert '自动补光输出开启' in auto.status()['events'][0]['reason']
