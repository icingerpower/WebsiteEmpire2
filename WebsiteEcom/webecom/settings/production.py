"""
Production settings for pradize project.
All secrets and DSNs come from environment variables — nothing is hardcoded here.
Requires: SECRET_KEY, DATABASE_URL (PostgreSQL DSN), ALLOWED_HOSTS (comma-separated).
"""

import os

from django.core.exceptions import ImproperlyConfigured

from .base import *  # noqa: F401, F403

DEBUG = False

SECRET_KEY = os.environ["SECRET_KEY"]

# ALLOWED_HOSTS is a comma-separated list in the environment, e.g.
# "pradize.com,www.pradize.com"
_allowed = os.environ.get("ALLOWED_HOSTS", "")
ALLOWED_HOSTS = [h.strip() for h in _allowed.split(",") if h.strip()]

# PostgreSQL connection via DATABASE_URL
# Format: postgres://user:password@host:port/dbname
_db_url = os.environ["DATABASE_URL"]

# Parse the DATABASE_URL manually to avoid adding the dj-database-url dependency.
# Expected format: postgres://user:pass@host:port/name
# (T001 scope: only Django is installed; a proper URL parser comes with T002/T003 infra.)
import urllib.parse  # noqa: E402

_parsed = urllib.parse.urlparse(_db_url)
DATABASES = {
    "default": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": _parsed.path.lstrip("/"),
        "USER": _parsed.username,
        "PASSWORD": _parsed.password,
        "HOST": _parsed.hostname,
        "PORT": str(_parsed.port or 5432),
        "CONN_MAX_AGE": 60,
        "OPTIONS": {
            "sslmode": os.environ.get("DB_SSLMODE", "require"),
        },
    },
    # Analytics DB — separate PostgreSQL instance (ADR-004).
    # Configure via ANALYTICS_DB_* environment variables.
    # When ANALYTICS_DATABASE_URL is set, its URL takes precedence
    # (dj-database-url is a future dependency; manual parse used here
    # to stay zero-dependency — mirrors the pattern above for 'default').
    "analytics": {
        "ENGINE": "django.db.backends.postgresql",
        "NAME": os.environ.get("ANALYTICS_DB_NAME", "pradize_analytics"),
        "USER": os.environ.get("ANALYTICS_DB_USER", ""),
        "PASSWORD": os.environ.get("ANALYTICS_DB_PASSWORD", ""),
        "HOST": os.environ.get("ANALYTICS_DB_HOST", "localhost"),
        "PORT": os.environ.get("ANALYTICS_DB_PORT", "5432"),
        "CONN_MAX_AGE": 60,
    },
}

# Security audit F3: base.py defaults to LocMemCache, which is per-process and
# non-atomic across gunicorn workers. pages/antispam.py's rate limiter (and, more
# generally, anything that relies on cache.incr() being atomic) is only a real
# security boundary when every worker shares one cache — so production MUST
# override it with a shared backend (Redis). Fails loudly at startup rather than
# silently deploying with a per-process counter (mirrors the PLATFORM_APEX_DOMAIN
# guard in base.py). CACHE_URL is a standard redis://[:password@]host:port/db DSN.
_cache_url = os.environ.get("CACHE_URL")
if not _cache_url:
    raise ImproperlyConfigured(
        "CACHE_URL must be set in the environment in production. A shared cache "
        "(Redis) is required so pages.antispam's rate limiter is atomic across "
        "all worker processes, not just best-effort per-process (security audit "
        "F3). Example: redis://localhost:6379/1"
    )
CACHES = {
    "default": {
        "BACKEND": "django.core.cache.backends.redis.RedisCache",
        "LOCATION": _cache_url,
    }
}

# KNOWN_RISKS.md #2 (docs/releases/pixels-currency-chat/): chat/anthropic_client.py's
# get_client() hands ANTHROPIC_API_KEY resolution to the SDK itself (it reads the
# env var lazily), so a missing key previously only surfaced on the first real
# chat call — ops got no boot-time signal. But chat is per-store opt-in AND
# platform-wide optional via CHAT_KILL_SWITCH (ADR-024 Decision 7): a deployment
# that never offers chat anywhere shouldn't be forced to configure a key it will
# never use. So this guard only fires when chat is potentially servable — kill
# switch off AND key missing — mirroring the CACHE_URL guard above (fail loudly
# at startup) without over-constraining platforms that keep chat fully disabled.
if not CHAT_KILL_SWITCH and not os.environ.get("ANTHROPIC_API_KEY"):
    raise ImproperlyConfigured(
        "ANTHROPIC_API_KEY must be set in the environment in production unless "
        "CHAT_KILL_SWITCH is enabled. Chat is per-store opt-in, so the key is "
        "only required while a call could actually be attempted somewhere on "
        "the platform (ADR-024 Decision 7). Set ANTHROPIC_API_KEY, or set "
        "CHAT_KILL_SWITCH=1/true/yes if this deployment never offers chat."
    )

# Security hardening
SECURE_SSL_REDIRECT = True
SESSION_COOKIE_SECURE = True
CSRF_COOKIE_SECURE = True
SECURE_HSTS_SECONDS = 31536000
SECURE_HSTS_INCLUDE_SUBDOMAINS = True
SECURE_HSTS_PRELOAD = True

# Static files — run `manage.py collectstatic` and serve via nginx/CDN.
STATIC_ROOT = BASE_DIR / "staticfiles"  # noqa: F405

# S3-compatible media storage — configure via environment variables
# DEFAULT_FILE_STORAGE = 'storages.backends.s3boto3.S3Boto3Storage'
# AWS_STORAGE_BUCKET_NAME = os.environ.get('AWS_STORAGE_BUCKET_NAME', '')
# AWS_S3_ENDPOINT_URL = os.environ.get('AWS_S3_ENDPOINT_URL', '')  # for non-AWS S3-compatible
# AWS_S3_CUSTOM_DOMAIN = os.environ.get('AWS_S3_CUSTOM_DOMAIN', '')
# MEDIA_URL = f'https://{AWS_S3_CUSTOM_DOMAIN}/'
# Note: add django-storages to requirements.txt when activating S3
