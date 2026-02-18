from pathlib import Path

import httpx
from fastapi import FastAPI, HTTPException
from fastapi.responses import FileResponse
from fastapi.staticfiles import StaticFiles

from .settings import settings


BASE_DIR = Path(__file__).resolve().parent
STATIC_DIR = BASE_DIR / "static"
INDEX_FILE = STATIC_DIR / "index.html"

app = FastAPI(title="System Metrics Dashboard", version="0.1.0")
app.mount("/static", StaticFiles(directory=STATIC_DIR), name="static")


@app.get("/health")
def health_check() -> dict:
    return {
        "status": "ok",
        "backend_url": settings.backend_base_url,
    }


@app.get("/")
def index() -> FileResponse:
    return FileResponse(INDEX_FILE)


@app.get("/api/metrics/recent")
async def proxy_recent_metrics() -> list[dict]:
    target = f"{settings.backend_base_url}/api/metrics/recent"
    timeout = httpx.Timeout(5.0)

    try:
        async with httpx.AsyncClient(timeout=timeout) as client:
            response = await client.get(target)
            response.raise_for_status()
    except httpx.HTTPStatusError as ex:
        detail = ex.response.text if ex.response is not None else str(ex)
        raise HTTPException(status_code=502, detail=f"Backend error: {detail}") from ex
    except httpx.HTTPError as ex:
        raise HTTPException(status_code=502, detail=f"Backend unavailable: {str(ex)}") from ex

    data = response.json()
    if not isinstance(data, list):
        raise HTTPException(status_code=502, detail="Unexpected backend response format")

    return data
