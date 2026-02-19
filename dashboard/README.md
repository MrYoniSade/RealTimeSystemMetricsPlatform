# Dashboard Service (FastAPI + JavaScript)

Phase 2 dashboard for visualizing real-time system metrics from the backend.

## Features
- Live metrics via WebSocket stream (`/ws/metrics` proxy -> backend)
- Bootstrap snapshot from `/api/metrics/recent`
- Total CPU usage chart
- Per-core CPU usage chart
- System memory chart (used vs total)
- Thread-count chart (aggregate of top processes)
- I/O chart (aggregate read/write MB)
- Handles chart (aggregate)
- Top-process table with CPU, memory, threads, I/O, and handles
- FastAPI proxy endpoints to avoid browser CORS issues

## Local Run

```bash
cd dashboard
python -m venv .venv
.venv\Scripts\activate
pip install -r requirements.txt
uvicorn app.main:app --host 0.0.0.0 --port 8080
```

Open: `http://localhost:8080`

## Configuration
- `BACKEND_BASE_URL` (default: `http://localhost:8000`)

## Docker Build
From repository root:

```bash
docker build -t metrics-dashboard:latest -f dashboard/Dockerfile .
```

Run:

```bash
docker run --rm -p 8080:8080 -e BACKEND_BASE_URL=http://host.docker.internal:8000 metrics-dashboard:latest
```
