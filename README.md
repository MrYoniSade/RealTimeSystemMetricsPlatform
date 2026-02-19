# Real-Time System Metrics Platform

## Project Overview
This project is an end-to-end observability stack for monitoring system metrics on a local machine.

Core components:
- C++ Metrics Agent
- Python Backend (FastAPI)
- Dashboard (FastAPI + JavaScript)
- Infrastructure (Docker + Kubernetes/Minikube)

## Architecture

### Agent (C++)
- Collects total CPU, per-core CPU, system/process memory, thread count, I/O, and handles.
- Sends metrics snapshots to backend over HTTP every configured interval.
- Supports multi-threaded collection/sending, config file loading, metric selection, and graceful shutdown.

### Backend (FastAPI)
- Ingest endpoint: `/ingest/metrics`
- Recent metrics endpoint: `/api/metrics/recent`
- Alerts endpoint: `/api/alerts/recent`
- Health endpoint: `/health`
- WebSocket endpoint: `/ws/metrics`
- Uses Redis for rolling window + Pub/Sub.
- Uses PostgreSQL for historical storage and retention pruning.
- Supports agent auth, rate limiting, alert rules, and OpenAPI docs (`/docs`).

### Dashboard (FastAPI + JS)
- Real-time visualization via WebSocket.
- CPU, per-core CPU, memory, threads, I/O, handles, and process table.
- Alert status/history, backend performance panel, runtime UI config.
- Session-based authentication and login/logout flow.

### Infrastructure
- Kubernetes resources in `metrics-app.yaml`:
  - Deployments: `backend`, `agent`, `dashboard`, `dummy-workloads`
  - StatefulSets: `redis`, `postgres`
  - Services: `backend`, `dashboard`, `redis`, `postgres`

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Agent | C++ |
| Backend | Python (FastAPI) |
| Dashboard | Python (FastAPI) + JavaScript (Chart.js) |
| In-Memory Cache | Redis |
| Historical Storage | PostgreSQL |
| Containerization | Docker |
| Orchestration | Kubernetes (Minikube / Docker Desktop K8s) |

## Key Design Decisions
1. Metrics interval: 2 seconds (configurable)
2. Data retention: rolling recent window in Redis + historical in PostgreSQL
3. Communication: HTTP for ingest, WebSocket for live dashboard updates
4. Deployment target: local Kubernetes (Minikube / Docker Desktop)
5. Top processes shown: up to 12 by CPU usage

## Implementation Roadmap (Merged from PLAN)

### Phase 1: Core Pipeline
#### Agent
- [x] Collect total CPU usage (overall)
- [x] Collect top processes by CPU usage
- [x] Send metrics JSON via HTTP POST every 2 seconds
- [x] Build containerized application

#### Backend
- [x] FastAPI app with Redis connection
- [x] `/ingest/metrics` for ingest + Redis storage
- [x] `/api/metrics/recent` for rolling recent data
- [x] Build containerized application

#### Dashboard
- [x] Web UI
- [x] Display total CPU and process table
- [x] Build containerized application

#### Infrastructure
- [x] Dockerfiles for all components
- [x] Local Docker build validation

### Phase 2: Real-Time + Richer Metrics
#### Agent
- [x] Per-core CPU
- [x] Memory (system/process)
- [x] Threads
- [x] I/O
- [x] Handles

#### Backend
- [x] WebSocket support
- [x] PostgreSQL connection and schema
- [x] PostgreSQL storage
- [x] Redis Pub/Sub broadcast path

#### Dashboard
- [x] Move from polling to WebSocket
- [x] Per-core CPU chart
- [x] Memory visualization
- [x] Thread visualization
- [x] I/O visualization
- [x] Handles visualization

#### Infrastructure
- [x] Redis + PostgreSQL in Kubernetes
- [x] StatefulSets and service wiring

### Phase 3: Advanced Features
#### Agent
- [x] Multi-threaded architecture (collect/send separation)
- [x] JSON/YAML config support
- [x] Configurable sampling interval
- [x] Configurable metric selection
- [x] Graceful shutdown
- [x] Structured logging

#### Backend
- [x] Agent rate limiting
- [x] Agent authentication/authorization token
- [x] Alert rule (CPU threshold duration)
- [x] Alert notification pipeline
- [x] Metrics retention policy
- [x] API documentation (OpenAPI/Swagger)

#### Dashboard
- [x] Alert status display
- [x] Alert history/logs
- [x] Runtime configuration UI
- [x] Backend performance metrics
- [x] User authentication

#### Infrastructure
- [x] Full Kubernetes manifests for all services
- [x] End-to-end deployment on local Kubernetes
- [x] Deployment documentation and helper scripts

## Success Criteria
- [x] Agent collects and sends metrics
- [x] Backend ingests and stores metrics
- [x] Dashboard shows real-time metrics
- [x] WebSocket live updates working
- [x] PostgreSQL historical retention working
- [x] Kubernetes deployment operational
- [x] Alert thresholds trigger as expected
- [x] Graceful shutdown path implemented

## Local Run (Component-by-Component)
1. Start backend:
   - `cd backend`
   - `python -m venv .venv`
   - `.venv\Scripts\activate`
   - `pip install -r requirements.txt`
   - `uvicorn app.main:app --host 0.0.0.0 --port 8000`
2. Start agent:
   - `cd agent`
   - build with `build.bat` (Windows) or `./build.sh` (Linux/macOS)
   - run `./build/metrics_agent --backend-url http://localhost:8000 --interval 2`
3. Start dashboard:
   - `cd dashboard`
   - `python -m venv .venv`
   - `.venv\Scripts\activate`
   - `pip install -r requirements.txt`
   - `uvicorn app.main:app --host 0.0.0.0 --port 8080`

## Kubernetes Runbook

### Deploy
1. Build images from repo root:
   - `docker build -t metrics-backend:latest -f backend/Dockerfile .`
   - `docker build -t metrics-agent:latest -f agent/Dockerfile agent`
   - `docker build -t metrics-dashboard:latest -f dashboard/Dockerfile .`
2. Run setup script:
   - `powershell -ExecutionPolicy Bypass -File .\scripts\setup-k8s.ps1`
3. Verify:
   - `kubectl get pods -o wide`
   - `kubectl get svc`

Dashboard URL (fixed NodePort):
- `http://localhost:30430`

### Graceful Shutdown / Teardown
- `powershell -ExecutionPolicy Bypass -File .\scripts\shutdown-k8s.ps1`

### Optional: Start synthetic extra workloads
- `powershell -ExecutionPolicy Bypass -File .\scripts\start-dummy-workloads.ps1`

## Repository Structure
- `agent/` — C++ collector/sender
- `backend/` — FastAPI ingest + storage + alerts
- `dashboard/` — FastAPI UI + JS charts
- `scripts/` — build/deploy/test helpers
- `metrics-app.yaml` — Kubernetes manifest
- `PLAN.md` — implementation plan source

