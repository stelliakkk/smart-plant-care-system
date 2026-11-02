"""Timed demo uses isolated storage and mocked MQTT; never real GPIO/AI."""
from datetime import datetime, timedelta
from concurrent.futures import ThreadPoolExecutor
import pytest
from fastapi.testclient import TestClient
from sqlalchemy import delete
from app.main import app, _on_status
from app.config import settings
from app.db.models import AutomationState, ActuatorCommand
from app.db.session import SessionLocal, init_db
from app.mqtt.client import mqtt_client
from app.mqtt import topics
from app.schemas.device import CommandRequest
from app.services import automation_service as auto, command_service as cs, device_service as ds

@pytest.fixture(autouse=True)
def isolated(monkeypatch):
    init_db()
    with SessionLocal() as db:
        db.execute(delete(AutomationState));db.execute(delete(ActuatorCommand));db.commit()
    ds._status_cache.clear()
    ds.mark_seen(settings.device_id,uptime_s=100,boot_id='1234abcd',control_ready=True,
        automation_ready=True,timed_pumps_ready=True,
        automation={n:False for n in ('pump','fertilizer','spray','grow_light')},
        actuators={n:False for n in ('pump','fertilizer','spray','grow_light')})
    packets=[]
    monkeypatch.setattr(mqtt_client,'_connected',True)
    monkeypatch.setattr(mqtt_client,'publish_json',lambda topic,payload,qos=0:packets.append(payload) or True)
    yield packets
    ds._status_cache.clear()

def due(name='pump', **values):
    with cs._lock,SessionLocal() as db:
        row,state=auto._load(db);state[auto.MODE_KEYS[name]]=True
        state['pump_schedules'][name].update(interval_sec=60,duration_ms=1000,
            next_run=(datetime.now()-timedelta(seconds=1)).isoformat())
        state['pump_schedules'][name].update(values)
        auto._save(db,row,state)
    ds.get_status().automation[name]=True

def ack(packet,result='done'):
    cs.acknowledge(topics.topic_command_ack(settings.device_id),dict(
        command_id=packet['command_id'],boot_id='1234abcd',actuator=packet['actuator'],result=result,reason='test'))

def test_defaults_disabled_and_unconfigured(isolated):
    state=auto.status()
    assert not state['pump_auto'] and not state['fertilizer_auto']
    with pytest.raises(ValueError):auto.configure('pump',True)
    auto.pump_tick();assert not isolated

def test_configure_pauses_and_resume_waits_full_interval(isolated):
    auto.configure_pump('pump',60,1000);auto.configure('pump',True)
    state=auto.status()
    assert state['pump_auto'] and datetime.fromisoformat(state['pump_schedules']['pump']['next_run'])>datetime.now()+timedelta(seconds=55)
    auto.pump_tick();assert not any(p['action']=='on' for p in isolated)
    auto.configure_pump('pump',120,2000)
    assert not auto.status()['pump_auto'] and not auto.status()['pump_schedules']['pump']['next_run']

@pytest.mark.parametrize('name',['pump','fertilizer'])
def test_due_runs_once_and_records_receipt(isolated,name):
    due(name);auto.pump_tick();auto.pump_tick()
    assert len(isolated)==1 and isolated[0]['source']=='auto' and isolated[0]['actuator']==name
    assert isolated[0]['duration_ms']==1000
    ack(isolated[0],'running');ack(isolated[0],'done')
    schedule=auto.status()['pump_schedules'][name]
    assert schedule['last_command_status']['result']=='done'
    assert datetime.fromisoformat(schedule['next_run'])>datetime.now()+timedelta(seconds=55)

def test_two_due_pumps_never_publish_two_on_commands(isolated):
    due('pump');due('fertilizer');auto.pump_tick()
    assert len(isolated)==1 and isolated[0]['actuator']=='pump'
    assert '互斥' in auto.status()['pump_schedules']['fertilizer']['last_reason']

def test_equal_intervals_alternate_over_many_cycles(isolated, monkeypatch):
    """Advance a simulated clock; equal schedules must not starve fertilizer."""
    class Clock(datetime):
        current = datetime.now()

        @classmethod
        def now(cls):
            return cls.current

    monkeypatch.setattr(auto, 'datetime', Clock)
    first_run = Clock.current + timedelta(seconds=60)
    for name in auto.PUMPS:
        due(name, next_run=first_run.isoformat())
    winners=[]
    for cycle in range(40):
        Clock.current = first_run + timedelta(seconds=60*cycle)
        before=len(isolated)
        auto.pump_tick()
        assert len(isolated)==before+1  # Pending command locks out the other pump.
        packet=isolated[-1]
        winners.append(packet['actuator'])
        ack(packet,'running')
        ack(packet,'done')
        auto.pump_tick()  # Repeated tick must not replay the skipped opportunity.
        assert len(isolated)==before+1
    assert winners==['pump','fertilizer']*20

def test_pump_turn_survives_restart_and_single_due_pump_can_run(isolated):
    due('pump');due('fertilizer');auto.pump_tick();ack(isolated[-1])
    auto.reset_sequence()
    assert auto.status()['last_scheduled_pump']=='pump'
    due('pump');due('fertilizer');auto.pump_tick()
    assert isolated[-1]['actuator']=='fertilizer'
    ack(isolated[-1])
    # The preferred pump is not due; the other must still run without waiting.
    due('fertilizer');auto.pump_tick()
    assert isolated[-1]['actuator']=='fertilizer' and len(isolated)==3

def test_manual_running_interlocks_timer_and_manual_other(isolated):
    cmd=cs.submit(CommandRequest(actuator='fertilizer',action='on',duration_ms=1000))
    assert cmd['sent']
    due('pump');auto.pump_tick()
    assert len(isolated)==1
    assert not cs.submit(CommandRequest(actuator='pump',action='on',duration_ms=1000))['sent']
    # An OFF command must always bypass the interlock.
    assert cs.submit(CommandRequest(actuator='pump',action='off'))['sent']

def test_board_running_or_unknown_other_prevents_start(isolated):
    due();ds.get_status().actuators['fertilizer']=True;auto.pump_tick();assert not isolated
    due();ds.get_status().actuators.pop('fertilizer');auto.pump_tick();assert not isolated

def test_offline_skips_and_does_not_catch_up(isolated):
    due();ds.get_status().last_seen=datetime.now()-timedelta(seconds=40)
    auto.pump_tick();assert not isolated
    assert '离线' in auto.status()['pump_schedules']['pump']['last_reason']
    ds.mark_seen(settings.device_id);auto.pump_tick();assert not isolated

def test_old_firmware_cannot_receive_new_mode_or_auto_command(isolated):
    due();ds.get_status().timed_pumps_ready=False
    auto.reconcile();auto.pump_tick()
    assert not isolated
    assert not cs.submit(CommandRequest(actuator='pump',action='auto_on'),internal=True)['sent']

def test_manual_operation_pauses_only_corresponding_timer(isolated):
    due('pump');due('fertilizer')
    cs.submit(CommandRequest(actuator='pump',action='off'))
    state=auto.status();assert not state['pump_auto'] and state['fertilizer_auto']
    assert state['pump_schedules']['pump']['next_run']==''

def test_restart_keeps_settings_and_modes_rebases_due_time(isolated):
    due('pump');due('fertilizer');auto.reset_sequence()
    state=auto.status()
    assert state['pump_auto'] and state['fertilizer_auto']
    assert state['pump_schedules']['pump']['interval_sec']==60
    auto.pump_tick();assert not isolated

def test_device_restart_rebases_schedule(isolated):
    due()
    _on_status('plant/edge01/status',dict(device_id=settings.device_id,boot_id='87654321',uptime_s=1,
        control_ready=True,automation_ready=True,timed_pumps_ready=True,
        automation={'pump':True,'fertilizer':False,'spray':False,'grow_light':False},
        actuators={'pump':False,'fertilizer':False,'spray':False,'grow_light':False}))
    auto.pump_tick();assert not any(p['action']=='on' for p in isolated)

def test_publish_failure_consumes_occurrence_and_never_retries(isolated,monkeypatch):
    due();calls=[]
    monkeypatch.setattr(mqtt_client,'publish_json',lambda *a,**k:calls.append(a) or False)
    auto.pump_tick();auto.pump_tick()
    assert len(calls)==1 and auto.status()['pump_schedules']['pump']['last_command_status']['result']=='failed'

def test_unknown_receipt_and_duplicate_ticks_do_not_repeat(isolated):
    due()
    with ThreadPoolExecutor(max_workers=4) as pool:list(pool.map(lambda _:auto.pump_tick(),range(4)))
    assert len(isolated)==1
    with SessionLocal() as db:
        row=db.get(ActuatorCommand,isolated[0]['command_id']);row.updated_at=datetime.now()-timedelta(seconds=20);db.commit()
    assert cs.history()[0]['result']=='timeout'
    auto.pump_tick();assert len(isolated)==1

@pytest.mark.parametrize('bad',[dict(interval_sec=0),dict(interval_sec=604801),dict(duration_ms=0),dict(duration_ms=30001),dict(interval_sec=True),dict(duration_ms=1.5)])
def test_api_rejects_invalid_schedule(isolated,bad):
    with TestClient(app) as client:
        body=dict(actuator='pump',interval_sec=60,duration_ms=1000);body.update(bad)
        assert client.post('/api/device/pump-schedule',json=body).status_code==422

def test_api_and_stop_all_pause_four_modes(isolated):
    with TestClient(app) as client:
        assert client.post('/api/device/automation',json=dict(actuator='pump',enabled=True)).status_code==400
        assert client.post('/api/device/pump-schedule',json=dict(actuator='pump',interval_sec=120,duration_ms=2000)).status_code==200
        assert client.post('/api/device/automation',json=dict(actuator='pump',enabled=True)).status_code==200
        due('fertilizer')
        client.post('/api/device/emergency-stop')
        state=client.get('/api/device/automation').json()
        assert all(not state[k] for k in auto.MODE_KEYS.values())

def test_status_boot_update_waits_for_command_lock(isolated):
    import threading
    started=threading.Event()
    due()
    data=dict(device_id=settings.device_id,boot_id='87654321',uptime_s=1,
        control_ready=True,automation_ready=True,timed_pumps_ready=True,
        automation={'pump':True,'fertilizer':False,'spray':False,'grow_light':False},
        actuators={'pump':False,'fertilizer':False,'spray':False,'grow_light':False})
    def update():
        started.set();_on_status('plant/edge01/status',data)
    with ThreadPoolExecutor(max_workers=1) as pool:
        with cs._lock:
            future=pool.submit(update);assert started.wait(1)
            assert ds.get_status().boot_id=='1234abcd'
        future.result(timeout=3)
    auto.pump_tick();assert not any(p['action']=='on' for p in isolated)

def test_completed_manual_command_releases_interlock(isolated):
    cmd=cs.submit(CommandRequest(actuator='fertilizer',action='on',duration_ms=1000))
    assert cmd['sent'];ack(isolated[0],'running');ack(isolated[0],'done')
    due();auto.pump_tick()
    assert len(isolated)==2 and isolated[1]['actuator']=='pump'
