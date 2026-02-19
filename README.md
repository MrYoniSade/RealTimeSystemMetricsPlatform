\# Real-Time System Metrics Platform



\## Project Overview

This project aims to build a small observability stack for monitoring system metrics on a local machine. It consists of three main components: a C++ agent for collecting metrics, a Python backend for processing and storing data, and a dashboard for visualizing the metrics in real-time. The entire system will be containerized and deployed using Minikube.



---

## Current Status (as of 2026-02-18)

- Phase 1 **Agent**: Implemented in `agent/`.
- Phase 1 **Backend**: Implemented in `backend/` with:
  - `POST /ingest/metrics`
  - `GET /api/metrics/recent`
  - Redis-backed 5-minute rolling window.
- Phase 1 **Dashboard**: Implemented in `dashboard/` with:
	- `GET /` dashboard UI
	- Polling every 2 seconds via dashboard proxy endpoint
	- Line chart for total CPU and table for top processes.

---

## Quick Start (Phase 1: Agent -> Backend -> Dashboard)

1. Start Redis locally (default port `6379`).
2. Start backend:
	- `cd backend`
	- `python -m venv .venv`
	- `.venv\Scripts\activate`
	- `pip install -r requirements.txt`
	- `uvicorn app.main:app --host 0.0.0.0 --port 8000`
3. Start agent from another terminal:
	- `cd agent`
	- build using `build.bat`
	- run `./build/metrics_agent --backend-url http://localhost:8000 --interval 2`
4. Start dashboard from another terminal:
	- `cd dashboard`
	- `python -m venv .venv`
	- `.venv\Scripts\activate`
	- `pip install -r requirements.txt`
	- `uvicorn app.main:app --host 0.0.0.0 --port 8080`
	- open `http://localhost:8080`

Backend details are documented in `backend/README.md`.
Dashboard details are documented in `dashboard/README.md`.

---

## Quick Start (Kubernetes: Redis + PostgreSQL + Backend + Agent + Dashboard)

Use the manifest `metrics-app.yaml` at the repository root.

1. Build local Docker images:
	- `docker build -t metrics-backend:latest ./backend`
	- `docker build -t metrics-agent:latest ./agent`
	- `docker build -t metrics-dashboard:latest -f dashboard/Dockerfile .`
2. Apply Kubernetes manifest:
	- `kubectl apply -f metrics-app.yaml`
3. Verify workloads:
	- `kubectl get pods`
	- `kubectl get statefulsets`
	- `kubectl get svc`
4. Access backend service:
	- If using Docker Desktop Kubernetes: open `http://localhost:8000`
	- If using Minikube: run `minikube service backend --url`
5. Access dashboard service:
	- If using Docker Desktop Kubernetes: open `http://localhost:8080`
	- If using Minikube: run `minikube service dashboard --url`

To stop/remove all resources:
	- `kubectl delete -f metrics-app.yaml`

### Phase 3 Deployment Runbook (Local Kubernetes)

Use the automated script:

1. Build fresh images (backend/agent/dashboard) from repo root.
2. Run:
	- `powershell -ExecutionPolicy Bypass -File .\scripts\setup-k8s.ps1`
3. Verify:
	- `kubectl get pods -o wide`
	- `kubectl get svc`
4. Open dashboard:
	- `http://localhost:30430`

Graceful shutdown test:

- Restart and observe healthy recovery:
	- `kubectl rollout restart deployment/agent`
	- `kubectl rollout restart deployment/backend`
	- `kubectl rollout restart deployment/dashboard`

- Tear down cleanly:
	- `kubectl delete -f metrics-app.yaml`

---



\## Architecture Overview



\### Components

1\. \*\*C++ Metrics Agent\*\*

&nbsp;  - Collects system metrics, including:

&nbsp;    - Total CPU usage (overall and per core).

&nbsp;    - Per-process CPU and memory usage.

&nbsp;    - Thread count, I/O, and handles.

&nbsp;  - Sends metrics to the backend via HTTP/REST.

&nbsp;  - Runs as a standalone containerized application.



2\. \*\*Python Backend Service (FastAPI)\*\*

&nbsp;  - Provides REST endpoints:

&nbsp;    - `/ingest/metrics`: Receives metrics from the agent.

&nbsp;    - `/api/metrics/recent`: Returns recent metrics for the dashboard.

&nbsp;  - WebSocket endpoint for real-time updates.

&nbsp;  - Stores metrics in:

&nbsp;    - \*\*Redis\*\* (in-memory time-series buffer and Pub/Sub for real-time updates).

&nbsp;    - \*\*PostgreSQL\*\* (for historical data storage).

&nbsp;  - Runs as a containerized FastAPI application.



3\. \*\*Dashboard Web App\*\*

&nbsp;  - Displays real-time metrics:

&nbsp;    - Line chart: Total CPU usage over time.

&nbsp;    - Multi-line chart: Per-core CPU usage.

&nbsp;    - Table: Top processes (name, PID, CPU%, memory).

&nbsp;    - Additional metrics: Thread count, I/O, and handles.

&nbsp;  - Connects to the backend via WebSocket for live updates.

&nbsp;  - Built with Python (FastAPI) and JavaScript (Chart.js or ECharts).



4\. \*\*Infrastructure\*\*

&nbsp;  - All components (agent, backend, Redis, PostgreSQL, and dashboard) will be containerized using Docker.

&nbsp;  - Deployment will be managed using \*\*Minikube\*\* (no Docker Compose).

&nbsp;  - Kubernetes manifests will define:

&nbsp;    - Deployments for the backend, agent, and dashboard.

&nbsp;    - StatefulSet for Redis and PostgreSQL.

&nbsp;    - Services for inter-component communication.



---



\## Implementation Plan



\### Phase 1: Core Pipeline

\- \*\*Agent\*\*:

&nbsp; - Collect total CPU usage and top 5 processes by CPU.

&nbsp; - Send metrics as JSON via HTTP POST every 2 seconds.

\- \*\*Backend\*\*:

&nbsp; - Implement `/ingest/metrics` endpoint to store metrics in Redis.

&nbsp; - Implement `/api/metrics/recent` to return the last 5 minutes of data.

\- \*\*Dashboard\*\*:

&nbsp; - Poll `/api/metrics/recent` every 2–3 seconds.

&nbsp; - Display:

&nbsp;   - Line chart: Total CPU usage over time.

&nbsp;   - Table: Top processes (name, PID, CPU%).



\### Phase 2: Real-Time Updates and Richer Metrics

\- \*\*Agent\*\*:

&nbsp; - Add collection of per-core CPU, memory usage, thread count, I/O, and handles.

\- \*\*Backend\*\*:

&nbsp; - Add WebSocket support for real-time updates.

&nbsp; - Store historical data in PostgreSQL.

\- \*\*Dashboard\*\*:

&nbsp; - Switch to WebSocket for real-time updates.

&nbsp; - Add charts for per-core CPU usage and other metrics.



\### Phase 3: Advanced Features

\- \*\*Agent\*\*:

&nbsp; - Use threads to separate metric collection and sending.

&nbsp; - Add a configuration file (JSON/YAML) for sampling intervals and metrics to collect.

&nbsp; - Implement graceful shutdown and logging.

\- \*\*Backend\*\*:

&nbsp; - Add rate limiting or authentication for the agent.

&nbsp; - Implement alert rules (e.g., CPU > 90% for 10 seconds triggers an alert).

\- \*\*Infrastructure\*\*:

&nbsp; - Create Kubernetes manifests for all components.

&nbsp; - Deploy the system on Minikube.



---



\## Deployment Details



\### Technologies Used

\- \*\*C++\*\*: For the metrics agent.

\- \*\*Python (FastAPI)\*\*: For the backend and dashboard.

\- \*\*Redis\*\*: For in-memory time-series buffering and Pub/Sub.

\- \*\*PostgreSQL\*\*: For historical data storage.

\- \*\*Docker\*\*: For containerizing all components.

\- \*\*Minikube\*\*: For local Kubernetes deployment.



\### Deployment Steps

1\. Build Docker images for all components (agent, backend, dashboard, Redis, PostgreSQL).

2\. Write Kubernetes manifests for:

&nbsp;  - Deployments (backend, agent, dashboard).

&nbsp;  - StatefulSets (Redis, PostgreSQL).

&nbsp;  - Services for inter-component communication.

3\. Deploy the system on Minikube.



---



\## How This Project Enhances Your CV

This project demonstrates your expertise in:

\- Building real-time systems with C++ and Python.

\- Using FastAPI for modern async APIs and WebSocket communication.

\- Managing in-memory and persistent data storage with Redis and PostgreSQL.

\- Containerizing applications with Docker.

\- Deploying and managing microservices with Kubernetes (Minikube).

