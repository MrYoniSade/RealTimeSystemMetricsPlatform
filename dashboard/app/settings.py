import os


class Settings:
    def __init__(self) -> None:
        self.backend_base_url = os.getenv("BACKEND_BASE_URL", "http://localhost:8000").rstrip("/")


settings = Settings()
