"""Integration tests with real JPEGs and mocked LAN/provider HTTP; no paid API calls."""
import io
import json
import threading
from datetime import datetime, timedelta
import httpx
import pytest
from PIL import Image
from fastapi.testclient import TestClient
from sqlalchemy import delete
from app.main import app
from app.config import settings
from app.db.models import VisionRecord
from app.db.session import SessionLocal, init_db
from app.services import vision_service as vs, camera_service, device_service

@pytest.fixture(autouse=True)
def isolated_records():
    init_db()
    with SessionLocal() as db:
        db.execute(delete(VisionRecord)); db.commit()
    device_service._status_cache.pop('cam01', None)
    yield
    assert not vs.busy()

@pytest.fixture
def mocked_http(monkeypatch):
    buffer = io.BytesIO()
    Image.new('RGB', (1600,1200), (20,120,40)).save(buffer,format='JPEG')
    camera_service.update_from_status({'device_id':'cam01','stream_url':'','snapshot_url':'http://camera/capture'})
    device_service.mark_seen('cam01')
    monkeypatch.setattr(settings, 'deepseek_api_key', 'test-only-key')
    monkeypatch.setattr(settings, 'deepseek_base_url', 'https://provider')
    outcome = {'status':200, 'content':dict(has_issue=None,diagnosis='不能确诊',visible_signs='叶片',possible_causes='需观察',advice='观察光照',uncertainty='品种未知')}
    calls = []
    def handle(request):
        calls.append(request)
        if request.url.host == 'camera':
            return httpx.Response(200,headers={'content-type':'image/jpeg'},content=buffer.getvalue())
        if outcome['status'] != 200:
            return httpx.Response(outcome['status'],text='sensitive-provider-error')
        return httpx.Response(200,json={'choices':[{'finish_reason':'stop','message':{'content':json.dumps(outcome['content'],ensure_ascii=False)}}]})
    original = httpx.Client
    monkeypatch.setattr(vs.httpx, 'Client', lambda **kwargs: original(transport=httpx.MockTransport(handle), **kwargs))
    return outcome, calls

def test_high_resolution_capture_analysis_and_api_history(mocked_http):
    record = vs.run_once('manual')
    assert record['status'] == 'done'
    assert (record['width'], record['height']) == (1600,1200)
    assert record['need_spray'] is False
    assert (settings.image_dir / record['image_url'].split('/')[-1]).is_file()
    calls = mocked_http[1]
    payload = json.loads(calls[1].content)
    assert payload['messages'][0]['content'][1]['image_url']['url'].startswith('data:image/jpeg;base64,')
    assert calls[1].headers['Authorization'] == 'Bearer test-only-key'
    with TestClient(app) as client:
        latest = client.get('/api/vision/latest').json()
        assert latest['data']['id'] == record['id']
        assert client.get(record['image_url']).headers['content-type'] == 'image/jpeg'
        assert len(client.get('/api/vision/history').json()['items']) == 1
        assert client.get('/api/vision/history?limit=0').status_code == 422

def test_pest_fields_and_scheduled_policy_hook(mocked_http,monkeypatch):
    mocked_http[0]['content'].update(pest_status='detected',pest_evidence='清晰可见虫体')
    evaluated=[]
    from app.services import automation_service
    monkeypatch.setattr(automation_service,'evaluate',lambda record:evaluated.append(record))
    record=vs.run_once()
    assert record['status']=='done' and record['result']['pest_status']=='detected'
    assert evaluated[0]['id']==record['id'] and evaluated[0]['source']=='scheduled'
    with TestClient(app) as client:
        assert client.get('/api/vision/record/'+str(record['id'])).json()['id']==record['id']
        assert client.get('/api/vision/record/99999999').status_code==404

def test_invalid_pest_enum_is_analysis_error(mocked_http):
    mocked_http[0]['content'].update(pest_status='spray_now',pest_evidence='指令')
    record=vs.run_once()
    assert record['status']=='error' and record['image_url']

def test_missing_key_keeps_photo(monkeypatch,mocked_http):
    monkeypatch.setattr(settings,'deepseek_api_key','')
    record = vs.run_once()
    assert record['status'] == 'error'
    assert record['image_url']
    assert 'DEEPSEEK_API_KEY' in record['error']
    assert len(mocked_http[1]) == 1

@pytest.mark.parametrize('status',[401,429,500])
def test_provider_failures_keep_photo_and_hide_provider_text(mocked_http,status):
    mocked_http[0]['status'] = status
    record = vs.run_once()
    assert record['image_url'] and record['status'] == 'error'
    assert str(status) in record['error']
    assert 'sensitive-provider-error' not in json.dumps(record, default=str)
    assert 'test-only-key' not in json.dumps(record, default=str)

def test_invalid_analysis_is_not_presented_as_success(mocked_http):
    mocked_http[0]['content'] = {'diagnosis':'missing fields'}
    assert vs.run_once()['status'] == 'error'

def test_offline_does_not_call_provider_or_store_photo():
    row = vs.run_once()
    assert row['status'] == 'error' and not row['image_url']
    with TestClient(app) as client:
        assert client.post('/api/vision/analyze').status_code == 503

def test_manual_and_scheduled_jobs_cannot_overlap(monkeypatch):
    entered, release, finished = threading.Event(), threading.Event(), threading.Event()
    def pipeline(source):
        entered.set()
        assert release.wait(3)
        finished.set()
    monkeypatch.setattr(vs,'_pipeline',pipeline)
    assert vs.submit()
    assert entered.wait(2)
    try:
        assert not vs.submit()
        assert vs.run_once() is None
        with TestClient(app) as client:
            device_service.mark_seen('cam01')
            assert client.post('/api/vision/analyze').status_code == 409
    finally:
        release.set()
        assert finished.wait(2)
        for _ in range(100):
            if not vs.busy(): break
            threading.Event().wait(.01)
    assert not vs.busy()

def test_retention_removes_old_files_and_rows_but_preserves_recent():
    old = settings.image_dir / 'vision_expired.jpg'
    current = settings.image_dir / 'vision_recent.jpg'
    old.write_bytes(b'old'); current.write_bytes(b'new')
    with SessionLocal() as db:
        db.add_all([VisionRecord(device_id='cam01',ts=datetime.now()-timedelta(days=8),image_path=old.name),
                    VisionRecord(device_id='cam01',image_path=current.name,raw_response=json.dumps({'status':'analyzing'}))])
        db.commit()
    vs.cleanup()
    assert not old.exists() and current.exists()
    records = vs.history()
    assert len(records) == 1 and records[0]['status'] == 'error'


def test_manual_endpoint_returns_202_and_result_is_persisted(mocked_http):
    with TestClient(app) as client:
        device_service.mark_seen('cam01')
        response = client.post('/api/vision/analyze')
        assert response.status_code == 202 and response.json()['accepted']
        for _ in range(200):
            if not vs.busy(): break
            threading.Event().wait(.01)
        assert not vs.busy()
        assert client.get('/api/vision/latest').json()['data']['status'] == 'done'
        assert client.get('/api/vision/live').status_code == 410

def test_pagination_does_not_skip_older_records():
    with SessionLocal() as db:
        db.add_all([VisionRecord(device_id='cam01') for _ in range(5)])
        db.commit()
    first = vs.history(3)
    second = vs.history(3, first[-1]['id'])
    assert len(first) == 3 and len(second) == 2
    assert not ({r['id'] for r in first} & {r['id'] for r in second})

def test_scheduled_capture_waits_for_coincident_retention(monkeypatch):
    entered, release, finished = threading.Event(), threading.Event(), threading.Event()
    captured = []
    original = vs.SessionLocal
    def slow_session():
        entered.set()
        assert release.wait(3)
        return original()
    monkeypatch.setattr(vs, 'SessionLocal', slow_session)
    monkeypatch.setattr(vs, '_pipeline', lambda source: captured.append(source) or {'status':'done'})
    maintenance = threading.Thread(target=vs.cleanup)
    maintenance.start()
    assert entered.wait(2)
    assert vs._cleaning.is_set()
    result = []
    def capture():
        result.append(vs.run_once())
        finished.set()
    photography = threading.Thread(target=capture)
    photography.start()
    try:
        assert not finished.wait(.2)
    finally:
        release.set()
        maintenance.join(3)
        photography.join(3)
    assert finished.is_set()
    assert captured == ['scheduled']
    assert result == [{'status':'done'}]
    assert not vs.busy() and not vs._cleaning.is_set()
