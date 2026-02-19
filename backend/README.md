# Python Backend Service (FastAPI)

Phase 1 + Phase 2 backend for ingesting metrics from the C++ agent, serving recent metrics, streaming live updates, and persisting historical data.

## Features
- `POST /ingest/metrics`: Receives and stores incoming metrics in Redis
- `GET /api/metrics/recent`: Returns metrics from the last 5 minutes
- `GET /health`: Reports backend connectivity to Redis and PostgreSQL
- `WS /ws/metrics`: Streams live metric payloads via Redis Pub/Sub
- Optional PostgreSQL persistence for historical metrics (`POSTGRES_DSN`)

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
- `POSTGRES_DSN` (default: empty/disabled)
- `POSTGRES_TABLE` (default: `metrics_snapshots`)

Example PostgreSQL DSN:

```bash
POSTGRES_DSN=postgresql://postgres:postgres@localhost:5432/metrics
```

When `POSTGRES_DSN` is set, the backend auto-creates a metrics table with indexes and inserts every ingested snapshot.

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
