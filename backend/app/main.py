"""Backend API entrypoints for ingesting, querying, alerting, and streaming system metrics."""

import importlib
import json
import logging
import re
import threading
from collections import defaultdict, deque
from datetime import datetime, timedelta, timezone
from typing import Deque, Dict, List, Tuple

from fastapi import Header, HTTPException, Query, Request, WebSocket, WebSocketDisconnect
from redis import Redis
from redis.asyncio import Redis as AsyncRedis
from redis.exceptions import RedisError

from .declarations import (
        AGENT_API_TOKEN,
        AGENT_RATE_LIMIT_PER_MINUTE,
        ALERTS_CHANNEL,
        ALERTS_KEY,
        ALERT_CPU_DURATION_SECONDS,
        ALERT_CPU_THRESHOLD,
        ALERT_RETENTION_SECONDS,
        HealthResponse,
        IngestResponse,
        METRICS_CHANNEL,
        METRICS_KEY,
        POSTGRES_DSN,
        POSTGRES_RETENTION_DAYS,
        POSTGRES_TABLE,
        REDIS_DB,
        REDIS_HOST,
        REDIS_PORT,
        RETENTION_SECONDS,
        AlertEvent,
        MetricsPayload,
        app,
)


logger = logging.getLogger(__name__)
_postgres_schema_ready = False
_agent_rate_windows: Dict[str, Deque[int]] = defaultdict(deque)
_rate_limit_lock = threading.Lock()
_alert_state_lock = threading.Lock()
_high_cpu_window_start_ts: int | None = None
_high_cpu_alert_active = False
_last_postgres_prune_epoch = 0


def get_redis() -> Redis:
        return Redis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def get_async_redis() -> AsyncRedis:
        return AsyncRedis(host=REDIS_HOST, port=REDIS_PORT, db=REDIS_DB, decode_responses=True)


def _load_psycopg_modules() -> Tuple[object, object]:
        try:
                psycopg = importlib.import_module("psycopg")
                json_types = importlib.import_module("psycopg.types.json")
        except ModuleNotFoundError as ex:
                raise HTTPException(
                        status_code=500,
                        detail="PostgreSQL storage configured but psycopg is not installed",
                ) from ex

        return psycopg, getattr(json_types, "Jsonb")


def _resolve_postgres_table_name() -> str:
        if re.fullmatch(r"[a-zA-Z_][a-zA-Z0-9_]*", POSTGRES_TABLE):
                return POSTGRES_TABLE
        return "metrics_snapshots"


def _ensure_postgres_schema(connection) -> None:
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
        if not POSTGRES_DSN:
                return

        psycopg, _ = _load_psycopg_modules()
        try:
                with psycopg.connect(POSTGRES_DSN, autocommit=True) as connection:
                        _ensure_postgres_schema(connection)
        except Exception as ex:
                raise HTTPException(status_code=503, detail=f"PostgreSQL unavailable: {str(ex)}") from ex


def apply_postgres_retention_policy(reference_time: datetime) -> None:
        global _last_postgres_prune_epoch

        if not POSTGRES_DSN or POSTGRES_RETENTION_DAYS <= 0:
                return

        epoch_now = int(reference_time.timestamp())
        if epoch_now - _last_postgres_prune_epoch < 60:
                return

        try:
                psycopg, _ = _load_psycopg_modules()
                cutoff = reference_time - timedelta(days=POSTGRES_RETENTION_DAYS)
                table_name = _resolve_postgres_table_name()

                with psycopg.connect(POSTGRES_DSN, autocommit=True) as connection:
                        _ensure_postgres_schema(connection)
                        with connection.cursor() as cursor:
                                cursor.execute(
                                        f"DELETE FROM {table_name} WHERE timestamp_utc < %s",
                                        (cutoff,),
                                )

                _last_postgres_prune_epoch = epoch_now
        except Exception as ex:
                logger.warning("PostgreSQL retention policy check failed: %s", str(ex))


def store_metrics_in_postgres(payload: MetricsPayload) -> None:
        if not POSTGRES_DSN:
                return

        psycopg, json_wrapper = _load_psycopg_modules()
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


def _agent_client_id(request: Request) -> str:
        if request.client and request.client.host:
                return request.client.host
        return "unknown-agent"


def enforce_agent_auth(x_agent_token: str | None) -> None:
        if not AGENT_API_TOKEN:
                return

        if not x_agent_token or x_agent_token != AGENT_API_TOKEN:
                raise HTTPException(status_code=401, detail="Invalid or missing agent token")


def enforce_rate_limit(client_id: str, request_time_epoch: int) -> None:
        if AGENT_RATE_LIMIT_PER_MINUTE <= 0:
                return

        with _rate_limit_lock:
                window = _agent_rate_windows[client_id]
                lower_bound = request_time_epoch - 60
                while window and window[0] < lower_bound:
                        window.popleft()

                if len(window) >= AGENT_RATE_LIMIT_PER_MINUTE:
                        raise HTTPException(status_code=429, detail="Agent rate limit exceeded")

                window.append(request_time_epoch)


def publish_alert(alert: AlertEvent) -> None:
        try:
                client = get_redis()
                serialized = alert.model_dump_json()
                min_ts = int((datetime.now(timezone.utc) - timedelta(seconds=ALERT_RETENTION_SECONDS)).timestamp())

                pipe = client.pipeline()
                pipe.zadd(ALERTS_KEY, {serialized: alert.timestamp})
                pipe.zremrangebyscore(ALERTS_KEY, "-inf", min_ts)
                pipe.expire(ALERTS_KEY, ALERT_RETENTION_SECONDS * 2)
                if hasattr(pipe, "publish"):
                        pipe.publish(ALERTS_CHANNEL, serialized)
                pipe.execute()
        except RedisError as ex:
                logger.warning("Alert publish failed: %s", str(ex))


def evaluate_cpu_alert_rule(payload: MetricsPayload) -> AlertEvent | None:
        global _high_cpu_window_start_ts, _high_cpu_alert_active

        with _alert_state_lock:
                current_cpu = float(payload.total_cpu_percent)
                if current_cpu >= ALERT_CPU_THRESHOLD:
                        if _high_cpu_window_start_ts is None:
                                _high_cpu_window_start_ts = payload.timestamp

                        elapsed = payload.timestamp - _high_cpu_window_start_ts
                        if elapsed >= ALERT_CPU_DURATION_SECONDS and not _high_cpu_alert_active:
                                _high_cpu_alert_active = True
                                return AlertEvent(
                                        timestamp=payload.timestamp,
                                        rule="cpu_threshold_duration",
                                        severity="warning",
                                        message=(
                                                f"CPU usage remained above {ALERT_CPU_THRESHOLD:.2f}% "
                                                f"for at least {ALERT_CPU_DURATION_SECONDS} seconds"
                                        ),
                                        current_value=current_cpu,
                                        threshold=ALERT_CPU_THRESHOLD,
                                )
                        return None

                _high_cpu_window_start_ts = None
                _high_cpu_alert_active = False
                return None


@app.on_event("startup")
def initialize_postgres_schema() -> None:
        if not POSTGRES_DSN:
                return

        try:
                check_postgres_connection()
        except HTTPException as ex:
                logger.warning("PostgreSQL schema initialization skipped: %s", ex.detail)


@app.get(
        "/health",
        response_model=HealthResponse,
        summary="Service health",
        description="Returns connectivity status for Redis and PostgreSQL.",
)
def health_check() -> dict:
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


@app.post(
        "/ingest/metrics",
        response_model=IngestResponse,
        summary="Ingest metrics snapshot",
        description="Receives metrics from the agent, applies auth/rate limits, stores in Redis/PostgreSQL, and evaluates alerts.",
)
def ingest_metrics(
        payload: MetricsPayload,
        request: Request,
        x_agent_token: str | None = Header(default=None, alias="X-Agent-Token"),
) -> dict:
        now = datetime.now(timezone.utc)
        now_epoch = int(now.timestamp())
        min_ts = int((now - timedelta(seconds=RETENTION_SECONDS)).timestamp())

        enforce_agent_auth(x_agent_token)
        enforce_rate_limit(_agent_client_id(request), now_epoch)

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
        apply_postgres_retention_policy(now)

        alert = evaluate_cpu_alert_rule(payload)
        if alert is not None:
                publish_alert(alert)

        return {"status": "accepted", "timestamp": payload.timestamp}


@app.get(
        "/api/metrics/recent",
        response_model=List[MetricsPayload],
        summary="Recent metrics",
        description="Returns metric snapshots retained in the active Redis rolling window.",
)
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


@app.get(
        "/api/alerts/recent",
        response_model=List[AlertEvent],
        summary="Recent alerts",
        description="Returns alert events from Redis within the requested recent window.",
)
def get_recent_alerts(minutes: int = Query(default=60, ge=1, le=1440)) -> List[AlertEvent]:
        now = datetime.now(timezone.utc)
        min_ts = int((now - timedelta(minutes=minutes)).timestamp())

        try:
                client = get_redis()
                raw_alerts = client.zrangebyscore(ALERTS_KEY, min_ts, "+inf")
        except RedisError as ex:
                raise HTTPException(status_code=503, detail=f"Redis unavailable: {str(ex)}") from ex

        parsed: List[AlertEvent] = []
        for item in raw_alerts:
                try:
                        parsed.append(AlertEvent.model_validate_json(item))
                except (ValueError, json.JSONDecodeError):
                        continue

        return parsed


@app.websocket("/ws/metrics")
async def metrics_updates_ws(websocket: WebSocket) -> None:
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
