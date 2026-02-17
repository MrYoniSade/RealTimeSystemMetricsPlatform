"""Unit tests for backend API endpoints with Redis mocked out.

These tests validate endpoint behavior without requiring a live Redis instance.
"""

import json
from datetime import datetime, timezone

from fastapi.testclient import TestClient
from redis.exceptions import RedisError

from backend.app import main as backend_main


class FakePipeline:
    """Minimal Redis pipeline stub used by ingest tests."""

    def __init__(self):
        self.zadd_payload = None
        self.zremrangebyscore_args = None
        self.expire_args = None
        self.executed = False

    def zadd(self, key, mapping):
        self.zadd_payload = (key, mapping)
        return self

    def zremrangebyscore(self, key, min_score, max_score):
        self.zremrangebyscore_args = (key, min_score, max_score)
        return self

    def expire(self, key, ttl):
        self.expire_args = (key, ttl)
        return self

    def execute(self):
        self.executed = True
        return [1, 0, 1]


class FakeRedisIngest:
    """Redis stub exposing ping/pipeline methods used by ingest and health tests."""

    def __init__(self):
        self.pipeline_instance = FakePipeline()

    def ping(self):
        return True

    def pipeline(self):
        return self.pipeline_instance


class FakeRedisRecent:
    """Redis stub exposing zrangebyscore for recent metrics retrieval tests."""

    def __init__(self, items):
        self.items = items

    def zrangebyscore(self, key, minimum, maximum):
        return self.items


class FakeRedisRecentSpy(FakeRedisRecent):
    """Redis stub that captures zrangebyscore call arguments for assertions."""

    def __init__(self, items):
        super().__init__(items)
        self.last_call = None

    def zrangebyscore(self, key, minimum, maximum):
        self.last_call = (key, minimum, maximum)
        return self.items


def sample_payload(ts: int | None = None):
    """Build a valid metrics payload for endpoint tests."""

    timestamp = ts or int(datetime.now(timezone.utc).timestamp())
    return {
        "timestamp": timestamp,
        "total_cpu_percent": 42.5,
        "top_processes": [
            {
                "pid": 123,
                "name": "python.exe",
                "cpu_percent": 10.2,
                "memory_mb": 256.4,
            }
        ],
    }


def test_health_ok(monkeypatch):
    """Returns healthy response when Redis ping succeeds."""

    fake_redis = FakeRedisIngest()
    monkeypatch.setattr(backend_main, "get_redis", lambda: fake_redis)

    client = TestClient(backend_main.app)
    response = client.get("/health")

    assert response.status_code == 200
    assert response.json() == {"status": "ok", "redis": "connected"}


def test_health_degraded_when_redis_down(monkeypatch):
    """Returns degraded response when Redis is unavailable."""

    def _raise():
        raise RedisError("connection failed")

    monkeypatch.setattr(backend_main, "get_redis", _raise)

    client = TestClient(backend_main.app)
    response = client.get("/health")

    assert response.status_code == 200
    assert response.json() == {"status": "degraded", "redis": "disconnected"}


def test_ingest_metrics_success(monkeypatch):
    """Stores incoming metrics in Redis sorted set and returns accepted."""

    fake_redis = FakeRedisIngest()
    monkeypatch.setattr(backend_main, "get_redis", lambda: fake_redis)

    payload = sample_payload()
    client = TestClient(backend_main.app)
    response = client.post("/ingest/metrics", json=payload)

    assert response.status_code == 200
    assert response.json() == {"status": "accepted", "timestamp": payload["timestamp"]}

    pipeline = fake_redis.pipeline_instance
    assert pipeline.executed is True
    assert pipeline.zadd_payload is not None
    assert pipeline.zremrangebyscore_args is not None
    assert pipeline.expire_args is not None
    assert pipeline.zadd_payload[0] == backend_main.METRICS_KEY
    assert pipeline.zremrangebyscore_args[0] == backend_main.METRICS_KEY
    assert pipeline.zremrangebyscore_args[1] == "-inf"
    assert pipeline.expire_args == (backend_main.METRICS_KEY, backend_main.RETENTION_SECONDS * 2)

    stored_json = next(iter(pipeline.zadd_payload[1].keys()))
    assert json.loads(stored_json)["timestamp"] == payload["timestamp"]


def test_ingest_metrics_redis_error(monkeypatch):
    """Returns 503 when ingest path cannot reach Redis."""

    def _raise():
        raise RedisError("redis unavailable")

    monkeypatch.setattr(backend_main, "get_redis", _raise)

    client = TestClient(backend_main.app)
    response = client.post("/ingest/metrics", json=sample_payload())

    assert response.status_code == 503
    assert "Redis unavailable" in response.json()["detail"]


def test_get_recent_metrics_returns_valid_and_skips_invalid(monkeypatch):
    """Parses valid JSON records and skips malformed records."""

    valid_payload = sample_payload()
    fake_items = [
        json.dumps(valid_payload),
        "not-json",
    ]

    monkeypatch.setattr(backend_main, "get_redis", lambda: FakeRedisRecent(fake_items))

    client = TestClient(backend_main.app)
    response = client.get("/api/metrics/recent")

    assert response.status_code == 200
    body = response.json()
    assert len(body) == 1
    assert body[0]["timestamp"] == valid_payload["timestamp"]


def test_get_recent_metrics_redis_error(monkeypatch):
    """Returns 503 when recent-metrics query cannot reach Redis."""

    def _raise():
        raise RedisError("redis unavailable")

    monkeypatch.setattr(backend_main, "get_redis", _raise)

    client = TestClient(backend_main.app)
    response = client.get("/api/metrics/recent")

    assert response.status_code == 503
    assert "Redis unavailable" in response.json()["detail"]


def test_get_recent_metrics_empty_result(monkeypatch):
    """Returns an empty list when no records are present in Redis."""

    monkeypatch.setattr(backend_main, "get_redis", lambda: FakeRedisRecent([]))

    client = TestClient(backend_main.app)
    response = client.get("/api/metrics/recent")

    assert response.status_code == 200
    assert response.json() == []


def test_get_recent_metrics_skips_schema_invalid_json(monkeypatch):
    """Skips JSON records that parse but do not match MetricsPayload schema."""

    valid_payload = sample_payload()
    missing_timestamp = json.dumps(
        {
            "total_cpu_percent": 40.0,
            "top_processes": [],
        }
    )

    fake_items = [missing_timestamp, json.dumps(valid_payload)]
    monkeypatch.setattr(backend_main, "get_redis", lambda: FakeRedisRecent(fake_items))

    client = TestClient(backend_main.app)
    response = client.get("/api/metrics/recent")

    assert response.status_code == 200
    body = response.json()
    assert len(body) == 1
    assert body[0]["timestamp"] == valid_payload["timestamp"]


def test_get_recent_metrics_uses_expected_score_bounds(monkeypatch):
    """Queries Redis using configured key and inclusive recent-window bounds."""

    fake_redis = FakeRedisRecentSpy([])
    monkeypatch.setattr(backend_main, "get_redis", lambda: fake_redis)

    client = TestClient(backend_main.app)
    response = client.get("/api/metrics/recent")

    assert response.status_code == 200
    assert fake_redis.last_call is not None
    assert fake_redis.last_call[0] == backend_main.METRICS_KEY
    assert fake_redis.last_call[2] == "+inf"
    assert isinstance(fake_redis.last_call[1], int)


def test_ingest_metrics_rejects_total_cpu_above_100():
    """Returns 422 when total_cpu_percent exceeds schema upper bound."""

    payload = sample_payload()
    payload["total_cpu_percent"] = 101.0

    client = TestClient(backend_main.app)
    response = client.post("/ingest/metrics", json=payload)

    assert response.status_code == 422


def test_ingest_metrics_rejects_too_many_top_processes():
    """Returns 422 when top_processes exceeds configured max length."""

    payload = sample_payload()
    payload["top_processes"] = [
        {
            "pid": index,
            "name": f"proc-{index}",
            "cpu_percent": 1.0,
            "memory_mb": 10.0,
        }
        for index in range(6)
    ]

    client = TestClient(backend_main.app)
    response = client.post("/ingest/metrics", json=payload)

    assert response.status_code == 422


def test_ingest_metrics_rejects_negative_process_memory():
    """Returns 422 when a process entry contains negative memory usage."""

    payload = sample_payload()
    payload["top_processes"][0]["memory_mb"] = -0.1

    client = TestClient(backend_main.app)
    response = client.post("/ingest/metrics", json=payload)

    assert response.status_code == 422
