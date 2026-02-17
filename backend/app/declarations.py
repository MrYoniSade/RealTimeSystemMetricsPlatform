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


class MetricsPayload(BaseModel):
    timestamp: int
    total_cpu_percent: float = Field(ge=0, le=100)
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
RETENTION_SECONDS = _parse_int_env("RETENTION_SECONDS", "300")


app = FastAPI(title="System Metrics Backend", version="0.1.0")
