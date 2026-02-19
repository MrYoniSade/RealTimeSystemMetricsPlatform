"""
Backend API entrypoints for ingesting and querying real-time system metrics.

This module implements the Phase-1 data path for the observability platform:

1) The C++ agent posts metric snapshots to `POST /ingest/metrics`.
2) The backend stores snapshots in Redis as a timestamp-sorted timeline.
3) Clients query recent data through `GET /api/metrics/recent`.

Design goals:
- Keep writes and reads lightweight for near-real-time operation.
- Retain only a rolling time window (default: last 5 minutes).
- Avoid hard dependency on local filesystem/database for Phase 1.

Redis data model:
- Key: configured by `REDIS_METRICS_KEY` (default: `metrics:timeline`)
- Type: Sorted Set (ZSET)
- Member: full JSON payload (serialized MetricsPayload)
- Score: payload timestamp (epoch seconds)

Why a ZSET:
- Efficient append-like inserts with timestamp score (`ZADD`)
- Efficient range queries for time windows (`ZRANGEBYSCORE`)
- Efficient pruning of old points (`ZREMRANGEBYSCORE`)

Retention policy:
- On each ingest, entries older than `RETENTION_SECONDS` are removed.
- The Redis key TTL is refreshed to `RETENTION_SECONDS * 2` so stale keys
    eventually disappear if ingestion stops.

Error handling strategy:
- Redis availability errors are surfaced as HTTP 503 so callers can retry.
- Health endpoint reports degraded state instead of throwing when Redis is down.
- Individual malformed records during read are skipped defensively.
"""

import json
import importlib
import logging
import re
from datetime import datetime, timedelta, timezone
from typing import List

from fastapi import HTTPException, WebSocket, WebSocketDisconnect
from redis import Redis
from redis.asyncio import Redis as AsyncRedis
from redis.exceptions import RedisError

from .declarations import (
        METRICS_KEY,
        METRICS_CHANNEL,
        POSTGRES_DSN,
        POSTGRES_TABLE,
        REDIS_DB,
        REDIS_HOST,
        REDIS_PORT,
        RETENTION_SECONDS,
        MetricsPayload,
        app,
)


logger = logging.getLogger(__name__)
_postgres_schema_ready = False


def get_redis() -> Redis:
        """
        Create and return a Redis client bound to configured host/port/database.

        Notes:
        - `decode_responses=True` ensures Redis returns `str` values instead of
            raw bytes, simplifying JSON parsing and response modeling.
        - Client construction is intentionally lightweight and done per request path;
            Redis-py internally manages socket connections.
        """
        return Redis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def get_async_redis() -> AsyncRedis:
        """Create and return an async Redis client for pub/sub and WebSocket streaming."""
        return AsyncRedis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def _resolve_postgres_table_name() -> str:
        """Return a safe SQL identifier for the configured metrics table name."""
        if re.fullmatch(r"[a-zA-Z_][a-zA-Z0-9_]*", POSTGRES_TABLE):
                return POSTGRES_TABLE
        return "metrics_snapshots"


def _ensure_postgres_schema(connection) -> None:
        """Create the metrics table/indexes when PostgreSQL storage is enabled."""
        global _postgres_schema_ready
        if _postgres_schema_ready:
                return

        table_name = _resolve_postgres_table_name()
        with connection.cursor() as cursor:
                cursor.execute(
                        f"""
                        CREATE TABLE IF NOT EXISTS {table_name} (
                                id BIGSERIAL PRIMARY KEY,
                                timestamp_utc TIMESTAMPTZ NOT NULL,
                                epoch_seconds BIGINT NOT NULL,
                                total_cpu_percent DOUBLE PRECISION NOT NULL,
                                per_core_cpu_percent JSONB NOT NULL DEFAULT '[]'::jsonb,
                                system_memory_total_mb DOUBLE PRECISION NOT NULL DEFAULT 0,
                                system_memory_used_mb DOUBLE PRECISION NOT NULL DEFAULT 0,
                                top_processes JSONB NOT NULL DEFAULT '[]'::jsonb,
                                created_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
                        )
                        """
                )
                cursor.execute(
                        f"""
                        CREATE INDEX IF NOT EXISTS idx_{table_name}_timestamp_utc
                        ON {table_name} (timestamp_utc DESC)
                        """
                )
                cursor.execute(
                        f"""
                        CREATE INDEX IF NOT EXISTS idx_{table_name}_created_at
                        ON {table_name} (created_at DESC)
                        """
                )

        _postgres_schema_ready = True


def check_postgres_connection() -> None:
        """Verify PostgreSQL connectivity and ensure schema readiness."""
        if not POSTGRES_DSN:
                return

        try:
                psycopg = importlib.import_module("psycopg")
        except ModuleNotFoundError as ex:
                raise HTTPException(
                        status_code=500,
                        detail="PostgreSQL storage configured but psycopg is not installed",
                ) from ex

        try:
                with psycopg.connect(POSTGRES_DSN, autocommit=True) as connection:
                        _ensure_postgres_schema(connection)
        except Exception as ex:
                raise HTTPException(status_code=503, detail=f"PostgreSQL unavailable: {str(ex)}") from ex


def store_metrics_in_postgres(payload: MetricsPayload) -> None:
        """Persist one metrics snapshot into PostgreSQL when a DSN is configured."""
        if not POSTGRES_DSN:
                return

        try:
                psycopg = importlib.import_module("psycopg")
                json_types = importlib.import_module("psycopg.types.json")
        except ModuleNotFoundError as ex:
                raise HTTPException(
                        status_code=500,
                        detail="PostgreSQL storage configured but psycopg is not installed",
                ) from ex

        json_wrapper = getattr(json_types, "Jsonb")

        table_name = _resolve_postgres_table_name()
        snapshot_time = datetime.fromtimestamp(payload.timestamp, tz=timezone.utc)
        top_processes = [process.model_dump() for process in payload.top_processes]

        try:
                with psycopg.connect(POSTGRES_DSN, autocommit=True) as connection:
                        _ensure_postgres_schema(connection)
                        with connection.cursor() as cursor:
                                cursor.execute(
                                        f"""
                                        INSERT INTO {table_name} (
                                                timestamp_utc,
                                                epoch_seconds,
                                                total_cpu_percent,
                                                per_core_cpu_percent,
                                                system_memory_total_mb,
                                                system_memory_used_mb,
                                                top_processes
                                        )
                                        VALUES (%s, %s, %s, %s, %s, %s, %s)
                                        """,
                                        (
                                                snapshot_time,
                                                payload.timestamp,
                                                payload.total_cpu_percent,
                                                json_wrapper(payload.per_core_cpu_percent),
                                                payload.system_memory_total_mb,
                                                payload.system_memory_used_mb,
                                                json_wrapper(top_processes),
                                        ),
                                )
        except Exception as ex:
                raise HTTPException(status_code=503, detail=f"PostgreSQL unavailable: {str(ex)}") from ex


@app.on_event("startup")
def initialize_postgres_schema() -> None:
        """Initialize PostgreSQL schema early when storage is enabled."""
        if not POSTGRES_DSN:
                return

        try:
                check_postgres_connection()
        except HTTPException as ex:
                logger.warning("PostgreSQL schema initialization skipped: %s", ex.detail)


@app.get("/health")
def health_check() -> dict:
        """
        Return service health with Redis connectivity status.

        Behavior:
        - If Redis responds to `PING`, returns `{"status": "ok", "redis": "connected"}`.
        - If Redis is unreachable/unavailable, returns
            `{"status": "degraded", "redis": "disconnected"}`.

        Rationale:
        - This endpoint is intended for liveness/readiness checks and operational
            dashboards. It intentionally avoids raising errors for degraded mode so
            health pollers can observe state transitions consistently.
        """
        try:
                client = get_redis()
                client.ping()
                response = {"status": "ok", "redis": "connected"}
        except RedisError:
                response = {"status": "degraded", "redis": "disconnected"}

        if not POSTGRES_DSN:
                response["postgres"] = "disabled"
                return response

        try:
                check_postgres_connection()
                response["postgres"] = "connected"
        except HTTPException:
                response["postgres"] = "disconnected"

        return response


@app.post("/ingest/metrics")
def ingest_metrics(payload: MetricsPayload) -> dict:
        """
        Ingest one metrics snapshot and maintain rolling-window retention.

        Request body:
        - `MetricsPayload` object generated by the agent, containing at least:
            timestamp, total CPU percentage, and top process list.

        Write path:
        - Compute minimum valid timestamp for the configured retention window.
        - Serialize payload once and use Redis pipeline for batched operations:
            1) `ZADD` insert current record using payload timestamp as score.
            2) `ZREMRANGEBYSCORE` prune records older than retention window.
            3) `EXPIRE` refresh key TTL to prevent unbounded stale key lifetime.

        Atomicity and performance:
        - The Redis pipeline reduces round-trips and keeps operations grouped.
            (Pipeline here is batching, not a Redis transaction.)

        Failure mode:
        - Any Redis error becomes HTTP 503 (`service unavailable`) so upstream
            senders can retry with backoff.

        Returns:
        - Acceptance acknowledgment including the ingested timestamp.
        """
        now = datetime.now(timezone.utc)
        min_ts = int((now - timedelta(seconds=RETENTION_SECONDS)).timestamp())

        try:
                client = get_redis()
                serialized = payload.model_dump_json()

                pipe = client.pipeline()
                pipe.zadd(METRICS_KEY, {serialized: payload.timestamp})
                pipe.zremrangebyscore(METRICS_KEY, "-inf", min_ts)
                if hasattr(pipe, "publish"):
                        pipe.publish(METRICS_CHANNEL, serialized)
                pipe.expire(METRICS_KEY, RETENTION_SECONDS * 2)
                pipe.execute()
        except RedisError as ex:
                raise HTTPException(status_code=503, detail=f"Redis unavailable: {str(ex)}") from ex

        store_metrics_in_postgres(payload)

        return {"status": "accepted", "timestamp": payload.timestamp}


@app.get("/api/metrics/recent", response_model=List[MetricsPayload])
def get_recent_metrics() -> List[MetricsPayload]:
        """
        Fetch metrics retained within the active rolling window.

        Query behavior:
        - Computes lower-bound timestamp as `now - RETENTION_SECONDS`.
        - Reads sorted-set members with score >= lower bound via `ZRANGEBYSCORE`.

        Parsing behavior:
        - Each Redis item is parsed as `MetricsPayload` JSON.
        - Malformed entries are skipped instead of failing the whole request,
            preserving API availability when partial corruption exists.

        Response:
        - Ordered list of `MetricsPayload` entries suitable for near-real-time
            charting and lightweight analytics.

        Failure mode:
        - Redis access failures are converted to HTTP 503.
        """
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


@app.websocket("/ws/metrics")
async def metrics_updates_ws(websocket: WebSocket) -> None:
        """Broadcast live metric snapshots using Redis pub/sub."""
        await websocket.accept()
        client = get_async_redis()
        pubsub = client.pubsub()

        try:
                await pubsub.subscribe(METRICS_CHANNEL)
                async for message in pubsub.listen():
                        if message.get("type") != "message":
                                continue
                        payload = message.get("data")
                        if payload is None:
                                continue
                        await websocket.send_text(str(payload))
        except (WebSocketDisconnect, RedisError):
                pass
        finally:
                try:
                        await pubsub.unsubscribe(METRICS_CHANNEL)
                        await pubsub.close()
                        await client.aclose()
                except RedisError:
                        pass
