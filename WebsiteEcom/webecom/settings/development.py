"""
Development settings for pradize project.
Uses SQLite, DEBUG=True, and a local insecure secret key.
Never deploy this settings module to production.
"""

import base64
import os

# Set platform apex domain before base.py is imported so the ImproperlyConfigured
# guard does not trigger in development.
os.environ.setdefault("PLATFORM_APEX_DOMAIN", "webecom.local")

from .base import *  # noqa: F401, F403

DEBUG = True

# A development-only secret key.  The production key must come from the environment.
SECRET_KEY = "django-insecure-dev-only-key-do-not-use-in-production"

ALLOWED_HOSTS = ["localhost", "127.0.0.1"]

DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": BASE_DIR / "db.sqlite3",  # noqa: F405
    },
    "analytics": {
        "ENGINE": "django.db.backends.sqlite3",
        "NAME": BASE_DIR / "analytics.db",  # noqa: F405
    },
}

# Fixed 32-byte dev key — base64url-encoded.  NEVER use in production.
# Falls back to the env var if set (allows CI to inject its own key).
if not FERNET_KEY:  # noqa: F405
    FERNET_KEY = base64.urlsafe_b64encode(b"pradize-dev-key-not-for-prod-000").decode()
