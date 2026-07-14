"""
Celery application for WebsiteEcom.

All Celery settings are read from Django's settings using the CELERY_ prefix
(e.g. CELERY_BROKER_URL, CELERY_BEAT_SCHEDULE).

Start the worker:
    celery -A webecom worker -l info

Start beat (scheduled tasks):
    celery -A webecom beat -l info
"""

import os

from celery import Celery

# Use development settings by default; override in production via DJANGO_SETTINGS_MODULE.
os.environ.setdefault("DJANGO_SETTINGS_MODULE", "webecom.settings.development")

app = Celery("webecom")

# Read all Celery configuration from Django settings using the CELERY_ namespace prefix.
app.config_from_object("django.conf:settings", namespace="CELERY")

# Automatically discover tasks in every app listed in INSTALLED_APPS.
app.autodiscover_tasks()
