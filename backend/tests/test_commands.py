"""Command lifecycle tests use isolated storage and mocked MQTT, never real outputs."""
from datetime import datetime, timedelta
import pytest
from fastapi.testclient import TestClient
from sqlalchemy import delete
from app.main import app, _on_status
from app.config import settings
from app.db.models import ActuatorCommand
from app.db.session import SessionLocal, init_db
from app.mqtt.client import mqtt_client
from app.mqtt import topics
from app.services import command_service as cs, device_service as ds
from app.schemas.device import CommandRequest

@pytest.fixture(autouse=True)
def setup(monkeypatch):
    init_db()
    with SessionLocal() as db:
        db.execute(delete(ActuatorCommand));db.commit()
    ds._status_cache.pop(settings.device_id,None)
    ds._uptime_seen.pop(settings.device_id,None)
    monkeypatch.setattr(mqtt_client,'_connected',True)
    yield
    ds._status_cache.pop(settings.device_id,None)

@pytest.fixture
def ready(monkeypatch):
    ds.mark_seen(settings.device_id,uptime_s=100,boot_id='1234abcd',control_ready=True,
                 actuators={'pump':False,'fertilizer':False,'spray':False,'grow_light':False})
    packets=[]
    monkeypatch.setattr(mqtt_client,'publish_json',lambda topic,payload,qos=0: packets.append((topic,payload,qos)) or True)
    return packets

def ack(record,result,**overrides):
    cs.acknowledge(topics.topic_command_ack(settings.device_id),dict(command_id=record['command_id'],
        actuator=record['command']['actuator'],boot_id='1234abcd',result=result,reason='test',**overrides))

def test_qos_deadline_and_running_completion(ready):
    record=cs.submit(CommandRequest(actuator='pump',action='on',duration_ms=1500))
    assert record['sent'] and ready[0][2]==1
    payload=ready[0][1]
    assert payload['boot_id']=='1234abcd' and 114000<=payload['deadline_uptime_ms']<=116000
    assert len(payload['command_id'])==32
    ack(record,'running');assert cs.history()[0]['result']=='running'
    ack(record,'done');assert cs.history()[0]['result']=='done'
    ack(record,'running');assert cs.history()[0]['result']=='done'

def test_offline_and_old_firmware_do_not_publish(monkeypatch):
    monkeypatch.setattr(mqtt_client,'publish_json',lambda *a,**k: pytest.fail('must not publish'))
    cmd=CommandRequest(actuator='pump',action='on',duration_ms=1000)
    assert not cs.submit(cmd)['sent']
    ds.mark_seen(settings.device_id)
    assert '固件' in cs.submit(cmd)['reason']

@pytest.mark.parametrize('duration',[0,30001])
def test_pump_limits_on_both_request_paths(ready,duration):
    assert not cs.submit(CommandRequest(actuator='spray',action='on',duration_ms=duration))['sent']
    assert not ready

def test_continuous_light_and_off(ready):
    record=cs.submit(CommandRequest(actuator='grow_light',action='on',duration_ms=0))
    assert record['sent']
    ack(record,'running')
    with SessionLocal() as db:
        row=db.get(ActuatorCommand,record['command_id']);row.updated_at=datetime.now()-timedelta(hours=1);db.commit()
    assert cs.history()[0]['result']=='running'
    off=cs.submit(CommandRequest(actuator='grow_light',action='off',duration_ms=1000))
    assert off['sent'] and ready[-1][1]['duration_ms']==0

def test_unknown_mismatched_or_previous_boot_ack_is_ignored(ready):
    record=cs.submit(CommandRequest(actuator='pump',action='on',duration_ms=1000))
    data=dict(command_id=record['command_id'],actuator='pump',boot_id='wrong',result='done')
    cs.acknowledge(topics.topic_command_ack(settings.device_id),data)
    data.update(boot_id='1234abcd',actuator='spray')
    cs.acknowledge(topics.topic_command_ack(settings.device_id),data)
    data.update(actuator='pump')
    cs.acknowledge('plant/other/cmd_ack',data)
    assert cs.history()[0]['result']=='pending'

def test_pending_timeout_and_late_ack(ready):
    record=cs.submit(CommandRequest(actuator='pump',action='on',duration_ms=1000))
    with SessionLocal() as db:
        row=db.get(ActuatorCommand,record['command_id']);row.updated_at=datetime.now()-timedelta(seconds=16);db.commit()
    assert cs.history()[0]['result']=='timeout'
    ack(record,'done');assert cs.history()[0]['result']=='done'

def test_publish_failure_persists_failure(ready,monkeypatch):
    monkeypatch.setattr(mqtt_client,'publish_json',lambda *a,**k:False)
    assert not cs.submit(CommandRequest(actuator='pump',action='off'))['sent']
    assert cs.history()[0]['result']=='failed'

def test_status_contract_and_api_stop_all(ready):
    with TestClient(app) as client:
        ds.mark_seen(settings.device_id,uptime_s=100,boot_id='1234abcd',control_ready=True)
        response=client.post('/api/device/command',json=dict(actuator='pump',action='on',duration_ms=1000))
        assert response.status_code==200 and response.json()['sent']
        assert client.get('/api/device/control-log').json()['items'][0]['command_id']==response.json()['command_id']
        assert client.post('/api/device/emergency-stop').json()['sent']
        assert len(ready)==5
        assert all(p[1]['action']=='off' for p in ready[1:])
        assert client.post('/api/device/command',json=dict(actuator='pump',action='on',duration_ms=True)).status_code==422

def test_status_marks_firmware_capability():
    _on_status('plant/edge01/status',dict(device_id=settings.device_id,uptime_s=50,
                boot_id='1234abcd',control_ready=True,actuators={'pump':True}))
    status=ds.get_status()
    assert status.control_ready and status.actuators['pump']
