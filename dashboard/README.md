# Dashboard Service (FastAPI + JavaScript)

Phase 1 dashboard for visualizing near-real-time system metrics from the backend.

## Features
- Polls metrics every 2 seconds from backend endpoint
- Line chart for total CPU usage over time
- Table for top processes (name, PID, CPU%, memory)
- Lightweight FastAPI proxy endpoint to avoid browser CORS issues

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
