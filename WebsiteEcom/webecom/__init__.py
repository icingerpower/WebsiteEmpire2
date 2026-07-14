# Load the Celery app when Django starts so @shared_task decorators register correctly.
try:
    from .celery import app as celery_app  # noqa: F401
    __all__ = ("celery_app",)
except ImportError:
    # Celery not installed — app unavailable (tests, minimal dev environments).
    pass
