# Python Backend Service (FastAPI)

Phase 1 backend for ingesting metrics from the C++ agent and serving recent metrics for the dashboard.

## Features
- `POST /ingest/metrics`: Receives and stores incoming metrics in Redis
- `GET /api/metrics/recent`: Returns metrics from the last 5 minutes
- `GET /health`: Basic service/Redis health check

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
- `RETENTION_SECONDS` (default: `300`)

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
