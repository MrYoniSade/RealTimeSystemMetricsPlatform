from pathlib import Path
from time import perf_counter
from typing import Any

import httpx
from fastapi import FastAPI, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import FileResponse, RedirectResponse
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, Field
from starlette.middleware.sessions import SessionMiddleware
from websockets.client import connect as ws_connect

from .settings import settings


BASE_DIR = Path(__file__).resolve().parent
STATIC_DIR = BASE_DIR / "static"
INDEX_FILE = STATIC_DIR / "index.html"
LOGIN_FILE = STATIC_DIR / "login.html"


class LoginPayload(BaseModel):
    username: str
    password: str


class DashboardConfig(BaseModel):
    max_points: int = Field(ge=30, le=1000)
    alerts_window_minutes: int = Field(ge=1, le=1440)
    perf_poll_seconds: int = Field(ge=2, le=300)


runtime_config = DashboardConfig(
    max_points=settings.default_max_points,
    alerts_window_minutes=settings.default_alert_minutes,
    perf_poll_seconds=settings.default_perf_poll_seconds,
)

app = FastAPI(title="System Metrics Dashboard", version="0.1.0")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")
app.add_middleware(SessionMiddleware, secret_key=settings.session_secret)


def _backend_ws_metrics_url() -> str:
    if settings.backend_base_url.startswith("https://"):
        return f"wss://{settings.backend_base_url[len('https://'):]}/ws/metrics"
    return f"ws://{settings.backend_base_url[len('http://'):]}/ws/metrics"


def _require_auth(request: Request) -> None:
    if not settings.auth_enabled:
        return

    if request.session.get("authenticated") is not True:
        raise HTTPException(status_code=401, detail="Authentication required")


async def _proxy_backend_get(path: str, params: dict[str, Any] | None = None) -> Any:
    target = f"{settings.backend_base_url}{path}"
    timeout = httpx.Timeout(5.0)

    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            response = await client.get(target, params=params)
            response.raise_for_status()
    except httpx.HTTPStatusError as ex:
        detail = ex.response.text if ex.response is not None else str(ex)
        raise HTTPException(status_code=502, detail=f"Backend error: {detail}") from ex
    except httpx.HTTPError as ex:
        raise HTTPException(status_code=502, detail=f"Backend unavailable: {str(ex)}") from ex

    return response.json()


@app.get("/health")
def health_check() -> dict:
    return {
        "status": "ok",
        "backend_url": settings.backend_base_url,
    }


@app.get("/login", response_model=None)
def login_page(request: Request):
    if not settings.auth_enabled:
        return RedirectResponse(url="/", status_code=302)

    if request.session.get("authenticated") is True:
        return RedirectResponse(url="/", status_code=302)

    return FileResponse(LOGIN_FILE)


@app.post("/auth/login")
def login(payload: LoginPayload, request: Request) -> dict:
    if not settings.auth_enabled:
        request.session["authenticated"] = True
        request.session["username"] = "anonymous"
        return {"status": "ok"}

    if payload.username != settings.dashboard_username or payload.password != settings.dashboard_password:
        raise HTTPException(status_code=401, detail="Invalid username or password")

    request.session["authenticated"] = True
    request.session["username"] = payload.username
    return {"status": "ok"}


@app.post("/auth/logout")
def logout(request: Request) -> dict:
    request.session.clear()
    return {"status": "ok"}


@app.get("/auth/session")
def auth_session(request: Request) -> dict:
    return {
        "authenticated": (request.session.get("authenticated") is True) or (not settings.auth_enabled),
        "username": request.session.get("username", ""),
        "auth_enabled": settings.auth_enabled,
    }


@app.get("/", response_model=None)
def index(request: Request):
    if settings.auth_enabled and request.session.get("authenticated") is not True:
        return RedirectResponse(url="/login", status_code=302)
    return FileResponse(INDEX_FILE)


@app.get("/api/metrics/recent")
async def proxy_recent_metrics(request: Request) -> list[dict]:
    _require_auth(request)
    data = await _proxy_backend_get("/api/metrics/recent")
    if not isinstance(data, list):
        raise HTTPException(status_code=502, detail="Unexpected backend response format")

    return data


@app.get("/api/alerts/recent")
async def proxy_recent_alerts(request: Request, minutes: int = 60) -> list[dict]:
    _require_auth(request)
    data = await _proxy_backend_get("/api/alerts/recent", params={"minutes": minutes})
    if not isinstance(data, list):
        raise HTTPException(status_code=502, detail="Unexpected backend response format")
    return data


@app.get("/api/backend/performance")
async def backend_performance(request: Request) -> dict:
    _require_auth(request)

    target = f"{settings.backend_base_url}/health"
    timeout = httpx.Timeout(5.0)

    started = perf_counter()
    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            response = await client.get(target)
            response.raise_for_status()
    except httpx.HTTPStatusError as ex:
        detail = ex.response.text if ex.response is not None else str(ex)
        raise HTTPException(status_code=502, detail=f"Backend error: {detail}") from ex
    except httpx.HTTPError as ex:
        raise HTTPException(status_code=502, detail=f"Backend unavailable: {str(ex)}") from ex

    latency_ms = round((perf_counter() - started) * 1000.0, 2)
    health = response.json() if isinstance(response.json(), dict) else {}

    return {
        "latency_ms": latency_ms,
        "status": health.get("status", "unknown"),
        "redis": health.get("redis", "unknown"),
        "postgres": health.get("postgres", "unknown"),
    }


@app.get("/api/dashboard/config")
def get_dashboard_config(request: Request) -> dict:
    _require_auth(request)
    return runtime_config.model_dump()


@app.put("/api/dashboard/config")
def update_dashboard_config(payload: DashboardConfig, request: Request) -> dict:
    _require_auth(request)

    runtime_config.max_points = payload.max_points
    runtime_config.alerts_window_minutes = payload.alerts_window_minutes
    runtime_config.perf_poll_seconds = payload.perf_poll_seconds
    return runtime_config.model_dump()


@app.websocket("/ws/metrics")
async def proxy_live_metrics(websocket: WebSocket) -> None:
    if settings.auth_enabled and websocket.session.get("authenticated") is not True:
        await websocket.close(code=4401)
        return

    await websocket.accept()
    target = _backend_ws_metrics_url()

    try:
        async with ws_connect(target) as backend_ws:
            async for message in backend_ws:
                await websocket.send_text(str(message))
    except WebSocketDisconnect:
        return
    except Exception:
        await websocket.close(code=1011)
