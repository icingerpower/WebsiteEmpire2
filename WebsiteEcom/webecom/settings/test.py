"""
Test settings for pradize project.

Inherits from development settings; uses in-memory SQLite for speed
and predictable test isolation. Never use in production.
"""

import os

os.environ.setdefault("PLATFORM_APEX_DOMAIN", "webecom.test")

from .development import *  # noqa: F401, F403

# Override databases with in-memory SQLite for all test runs.
DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": ":memory:",
    },
    "analytics": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": ":memory:",
    },
}

# Disable password hashing for faster tests.
PASSWORD_HASHERS = [
    "django.contrib.auth.hashers.MD5PasswordHasher",
]

# Keep Celery tasks synchronous in tests — no worker needed.
CELERY_TASK_ALWAYS_EAGER = True
CELERY_TASK_EAGER_PROPAGATES = True
