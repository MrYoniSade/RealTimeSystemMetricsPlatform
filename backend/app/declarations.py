import os
import re
from typing import List

from fastapi import FastAPI
from pydantic import BaseModel, Field


class ProcessMetric(BaseModel):
    pid: int
    name: str
    cpu_percent: float = Field(ge=0)
    memory_mb: float = Field(ge=0)
    thread_count: int = Field(default=0, ge=0)
    io_read_mb: float = Field(default=0, ge=0)
    io_write_mb: float = Field(default=0, ge=0)
    handle_count: int = Field(default=0, ge=0)


class MetricsPayload(BaseModel):
    timestamp: int
    total_cpu_percent: float = Field(ge=0, le=100)
    per_core_cpu_percent: List[float] = Field(default_factory=list)
    system_memory_total_mb: float = Field(default=0, ge=0)
    system_memory_used_mb: float = Field(default=0, ge=0)
    top_processes: List[ProcessMetric] = Field(default_factory=list, max_length=5)


REDIS_HOST = os.getenv("REDIS_HOST", "localhost")


def _parse_int_env(name: str, default: str) -> int:
    raw = os.getenv(name, default)
    try:
        return int(raw)
    except (TypeError, ValueError):
        match = re.search(r"(\d+)$", str(raw))
        if match:
            return int(match.group(1))
        return int(default)


REDIS_PORT = _parse_int_env("REDIS_PORT", "6379")
REDIS_DB = _parse_int_env("REDIS_DB", "0")
METRICS_KEY = os.getenv("REDIS_METRICS_KEY", "metrics:timeline")
METRICS_CHANNEL = os.getenv("REDIS_METRICS_CHANNEL", "metrics:live")
RETENTION_SECONDS = _parse_int_env("RETENTION_SECONDS", "300")

POSTGRES_DSN = os.getenv("POSTGRES_DSN", "")
POSTGRES_TABLE = os.getenv("POSTGRES_TABLE", "metrics_snapshots")


app = FastAPI(title="System Metrics Backend", version="0.1.0")
