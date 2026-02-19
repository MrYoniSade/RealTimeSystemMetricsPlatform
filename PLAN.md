# Real-Time System Metrics Platform - Implementation Plan

## Project Overview
Build a small observability stack for monitoring system metrics on a local machine. The system consists of:
- **C++ Metrics Agent**: Collects system metrics
- **Python Backend (FastAPI)**: Processes and stores data
- **Dashboard**: Real-time visualization
- **Infrastructure**: Docker + Minikube deployment

---

## Phase 1: Core Pipeline
*Goal: Establish the basic end-to-end flow*

### Agent (C++)
- [x] Collect total CPU usage (overall)
- [x] Collect top 5 processes by CPU usage
- [x] Send metrics as JSON via HTTP POST every 2 seconds to backend
- [x] Build as containerized application

### Backend (Python/FastAPI)
- [x] Set up FastAPI application with Redis connection
- [x] Implement `/ingest/metrics` endpoint to receive and store metrics in Redis
- [x] Implement `/api/metrics/recent` endpoint to return last 5 minutes of data
- [x] Build as containerized application

### Dashboard
- [x] Create web interface (HTML/CSS/JavaScript)
- [x] Poll `/api/metrics/recent` every 2–3 seconds
- [x] Display line chart: Total CPU usage over time
- [x] Display table: Top processes (name, PID, CPU%)
- [x] Build as containerized application

### Infrastructure
- [x] Create Dockerfile for agent
- [x] Create Dockerfile for backend
- [x] Create Dockerfile for dashboard
- [x] Test local Docker builds

---

## Phase 2: Real-Time Updates and Richer Metrics
*Goal: Add WebSocket support and expand metrics*

### Agent (C++)
- [x] Collect per-core CPU usage
- [x] Collect memory usage (per-process and system)
- [x] Collect thread count
- [x] Collect I/O metrics
- [x] Collect handles

### Backend (Python/FastAPI)
- [x] Implement WebSocket endpoint for real-time updates
- [x] Set up PostgreSQL connection for historical data
- [x] Implement metrics storage in PostgreSQL
- [x] Implement Pub/Sub mechanism via Redis for WebSocket broadcasts
- [x] Design schema for metrics table in PostgreSQL

### Dashboard
- [x] Switch from polling to WebSocket for real-time updates
- [x] Add multi-line chart for per-core CPU usage
- [x] Add memory usage visualization
- [x] Add thread count visualization
- [x] Add I/O metrics visualization
- [x] Add handles visualization

### Infrastructure
- [x] Set up PostgreSQL container
- [x] Set up Redis container
- [x] Update Kubernetes manifests with StatefulSets for databases

---

## Phase 3: Advanced Features
*Goal: Production-ready system with advanced capabilities*

### Agent (C++)
- [x] Implement multi-threaded architecture (separate collection and sending)
- [x] Add JSON/YAML configuration file support
- [x] Make sampling intervals configurable
- [x] Allow selection of which metrics to collect
- [x] Implement graceful shutdown (signal handling)
- [x] Add structured logging

### Backend (Python/FastAPI)
- [x] Implement rate limiting for agent connections
- [x] Add authentication/authorization for agent
- [x] Implement alert rules (e.g., CPU > 90% for 10 seconds)
- [x] Add alert notification system
- [x] Implement metrics retention policy
- [x] Add API documentation (OpenAPI/Swagger)

### Dashboard
- [ ] Add alert status display
- [ ] Add alert history/logs
- [ ] Add configuration UI
- [ ] Add performance metrics for backend
- [ ] Implement user authentication

### Infrastructure
- [ ] Create Kubernetes Deployment manifests for backend
- [ ] Create Kubernetes Deployment manifests for agent
- [ ] Create Kubernetes Deployment manifests for dashboard
- [ ] Create Kubernetes StatefulSet manifests for Redis
- [ ] Create Kubernetes StatefulSet manifests for PostgreSQL
- [ ] Create Kubernetes Service manifests for inter-component communication
- [ ] Deploy complete system on Minikube
- [ ] Document deployment steps

---

## Technology Stack

| Component | Technology |
|-----------|-----------|
| Agent | C++ |
| Backend | Python (FastAPI) |
| Dashboard | Python (FastAPI) + JavaScript (Chart.js or ECharts) |
| In-Memory Cache | Redis |
| Historical Storage | PostgreSQL |
| Containerization | Docker |
| Orchestration | Kubernetes (Minikube) |

---

## Key Design Decisions

1. **Metrics Frequency**: 2-second intervals for collection and sending
2. **Data Retention**: Last 5 minutes in Redis (real-time), historical in PostgreSQL
3. **Communication**: HTTP/REST for agent→backend, WebSocket for backend→dashboard
4. **Deployment**: Minikube only (no Docker Compose)
5. **Top Processes**: Show top 5 by CPU usage

---

## Success Criteria

- [x] Agent collects and sends metrics successfully
- [x] Backend ingests and stores metrics
- [x] Dashboard displays real-time CPU metrics
- [x] WebSocket provides live updates without polling
- [x] PostgreSQL retains historical data
- [x] System deploys and runs on Minikube
- [x] Alerts trigger on defined thresholds
- [ ] System handles graceful shutdown
