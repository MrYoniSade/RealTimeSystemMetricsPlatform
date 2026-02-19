import os


class Settings:
    def __init__(self) -> None:
        self.backend_base_url = os.getenv("BACKEND_BASE_URL", "http://localhost:8000").rstrip("/")
        self.auth_enabled = os.getenv("DASHBOARD_AUTH_ENABLED", "true").lower() in {
            "1",
            "true",
            "yes",
            "on",
        }
        self.dashboard_username = os.getenv("DASHBOARD_USERNAME", "admin")
        self.dashboard_password = os.getenv("DASHBOARD_PASSWORD", "admin")
        self.session_secret = os.getenv("DASHBOARD_SESSION_SECRET", "dev-dashboard-secret")
        self.default_max_points = max(int(os.getenv("DASHBOARD_MAX_POINTS", "150")), 30)
        self.default_alert_minutes = max(int(os.getenv("DASHBOARD_ALERT_MINUTES", "60")), 1)
        self.default_perf_poll_seconds = max(int(os.getenv("DASHBOARD_PERF_POLL_SECONDS", "5")), 2)


settings = Settings()
