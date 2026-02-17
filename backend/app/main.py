import json
from datetime import datetime, timedelta, timezone
from typing import List

from fastapi import HTTPException
from redis import Redis
from redis.exceptions import RedisError

from .declarations import (
    METRICS_KEY,
    REDIS_DB,
    REDIS_HOST,
    REDIS_PORT,
    RETENTION_SECONDS,
    MetricsPayload,
    app,
)


def get_redis() -> Redis:
    return Redis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


@app.get("/health")
def health_check() -> dict:
    try:
        client = get_redis()
        client.ping()
        return {"status": "ok", "redis": "connected"}
    except RedisError:
        return {"status": "degraded", "redis": "disconnected"}


@app.post("/ingest/metrics")
def ingest_metrics(payload: MetricsPayload) -> dict:
    now = datetime.now(timezone.utc)
    min_ts = int((now - timedelta(seconds=RETENTION_SECONDS)).timestamp())

    try:
        client = get_redis()
        serialized = payload.model_dump_json()

        pipe = client.pipeline()
        pipe.zadd(METRICS_KEY, {serialized: payload.timestamp})
        pipe.zremrangebyscore(METRICS_KEY, "-inf", min_ts)
        pipe.expire(METRICS_KEY, RETENTION_SECONDS * 2)
        pipe.execute()
    except RedisError as ex:
        raise HTTPException(status_code=503, detail=f"Redis unavailable: {str(ex)}") from ex

    return {"status": "accepted", "timestamp": payload.timestamp}


@app.get("/api/metrics/recent", response_model=List[MetricsPayload])
def get_recent_metrics() -> List[MetricsPayload]:
    now = datetime.now(timezone.utc)
    min_ts = int((now - timedelta(seconds=RETENTION_SECONDS)).timestamp())

    try:
        client = get_redis()
        data = client.zrangebyscore(METRICS_KEY, min_ts, "+inf")
    except RedisError as ex:
        raise HTTPException(status_code=503, detail=f"Redis unavailable: {str(ex)}") from ex

    parsed: List[MetricsPayload] = []
    for item in data:
        try:
            parsed.append(MetricsPayload.model_validate_json(item))
        except (ValueError, json.JSONDecodeError):
            continue

    return parsed
