# Python Backend Service (FastAPI)

Phase 1 + Phase 2 + Phase 3 backend for ingesting metrics from the C++ agent, serving recent metrics, streaming live updates, persisting historical data, and evaluating alerts.

## Features
- `POST /ingest/metrics`: Receives and stores incoming metrics in Redis
- `GET /api/metrics/recent`: Returns metrics from the last 5 minutes
- `GET /api/alerts/recent`: Returns recent alert events
- `GET /health`: Reports backend connectivity to Redis and PostgreSQL
- `WS /ws/metrics`: Streams live metric payloads via Redis Pub/Sub
- Optional PostgreSQL persistence for historical metrics (`POSTGRES_DSN`)
- Optional agent auth token + ingest rate limiting
- CPU threshold-duration alert rule with Redis notification publish
- PostgreSQL retention pruning (days-based)

## Requirements
- Python 3.11+
- Redis running locally or reachable over network

## Local Run

```bash
cd backend
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8000
```

By default, Redis is expected at `localhost:6379`.

## Environment Variables
- `REDIS_HOST` (default: `localhost`)
- `REDIS_PORT` (default: `6379`)
- `REDIS_DB` (default: `0`)
- `REDIS_METRICS_KEY` (default: `metrics:timeline`)
- `REDIS_METRICS_CHANNEL` (default: `metrics:live`)
- `RETENTION_SECONDS` (default: `300`)
- `REDIS_ALERTS_KEY` (default: `alerts:timeline`)
- `REDIS_ALERTS_CHANNEL` (default: `alerts:live`)
- `ALERT_RETENTION_SECONDS` (default: `86400`)
- `POSTGRES_DSN` (default: empty/disabled)
- `POSTGRES_TABLE` (default: `metrics_snapshots`)
- `POSTGRES_RETENTION_DAYS` (default: `30`)
- `AGENT_API_TOKEN` (default: empty/disabled)
- `AGENT_RATE_LIMIT_PER_MINUTE` (default: `120`)
- `ALERT_CPU_THRESHOLD` (default: `90`)
- `ALERT_CPU_DURATION_SECONDS` (default: `10`)

Example PostgreSQL DSN:

```bash
POSTGRES_DSN=postgresql://postgres:postgres@localhost:5432/metrics
```

When `POSTGRES_DSN` is set, the backend auto-creates a metrics table with indexes and inserts every ingested snapshot.

If `AGENT_API_TOKEN` is set, `POST /ingest/metrics` requires `X-Agent-Token` header.

OpenAPI docs are available at:

```text
/docs
```

## Docker Build

From repository root:

```bash
docker build -t metrics-backend:latest -f backend/Dockerfile .
```

Run backend container:

```bash
docker run --rm -p 8000:8000 \
  -e REDIS_HOST=host.docker.internal \
  metrics-backend:latest
```

## API Payload Format

```json
{
  "timestamp": 1707662400,
  "total_cpu_percent": 45.2,
  "top_processes": [
    {
      "pid": 1234,
      "name": "chrome.exe",
      "cpu_percent": 15.5,
      "memory_mb": 512.3
    }
  ]
}
```
