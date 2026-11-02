"""Source provenance is stored per reading; all databases/MQTT are isolated."""
import uuid
from datetime import datetime, timedelta

from fastapi.testclient import TestClient
from sqlalchemy import create_engine, inspect, select, text
from sqlalchemy.orm import Session

from app.main import app, _on_telemetry
from app.db.models import SensorReading
from app.db import session as db_session
from scripts.sim_device import FakeSensors


def test_mqtt_and_http_sources_roundtrip():
    device='pytest-source-'+uuid.uuid4().hex[:8]
    with TestClient(app) as client:
        now=datetime.now()
        # Existing firmware omits the optional field and is still accepted.
        _on_telemetry('plant/edge01/telemetry',dict(
            device_id=device,soil_moisture=30,ts=now.isoformat()))
        assert client.get('/api/telemetry/latest',params={'device_id':device}).json()['data']['data_source']=='device'
        # HTTP simulation cannot accidentally claim to be a hardware reading.
        response=client.post('/api/telemetry/simulate',json=dict(
            device_id=device,soil_moisture=45,data_source='device',
            ts=(now+timedelta(seconds=1)).isoformat()))
        assert response.status_code==200
        assert client.get('/api/telemetry/latest',params={'device_id':device}).json()['data']['data_source']=='simulated'
        data=FakeSensors().step()
        data.update(device_id=device,ts=(now+timedelta(seconds=2)).isoformat())
        _on_telemetry('plant/edge01/telemetry',data)
        _on_telemetry('plant/edge01/telemetry',dict(
            device_id=device,soil_moisture=31,ts=(now+timedelta(seconds=3)).isoformat()))
        readings=client.get('/api/telemetry/history',params={'device_id':device}).json()['data']
        assert [r['data_source'] for r in readings]==['device','simulated','simulated','device']
        assert client.get('/api/telemetry/latest',params={'device_id':device}).json()['data']['data_source']=='device'


def test_old_database_migration_preserves_unknown_sources(monkeypatch):
    engine=create_engine('sqlite://')
    try:
        with engine.begin() as connection:
            connection.execute(text('''CREATE TABLE sensor_reading (
                id INTEGER PRIMARY KEY, device_id VARCHAR(32) NOT NULL,
                ts DATETIME NOT NULL, soil_moisture FLOAT, soil_ec FLOAT,
                temperature FLOAT, light_lux FLOAT)'''))
            connection.execute(text("""INSERT INTO sensor_reading
                (id,device_id,ts,soil_moisture) VALUES
                (1,'old-edge','2026-11-01 12:00:00',42.5)"""))
        monkeypatch.setattr(db_session,'engine',engine)
        db_session.init_db()
        db_session.init_db()  # Startup migration is repeatable.
        assert 'data_source' in {c['name'] for c in inspect(engine).get_columns('sensor_reading')}
        with Session(engine) as db:
            old=db.scalar(select(SensorReading))
            assert old.data_source=='unknown' and old.soil_moisture==42.5
            db.add(SensorReading(device_id='new-edge',soil_moisture=50))
            db.commit()
            assert db.scalar(select(SensorReading).where(SensorReading.device_id=='new-edge')).data_source=='device'
    finally:
        engine.dispose()
