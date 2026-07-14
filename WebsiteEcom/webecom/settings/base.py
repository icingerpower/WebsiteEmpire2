"""
Base settings for pradize project — shared by all environments.
Do not put environment-specific settings (DEBUG, databases, secrets) here.
"""

import os
from pathlib import Path

from celery.schedules import crontab
from django.core.exceptions import ImproperlyConfigured

# Build paths inside the project like this: BASE_DIR / 'subdir'.
BASE_DIR = Path(__file__).resolve().parent.parent.parent


# Application definition

INSTALLED_APPS = [
    "django.contrib.admin",
    "django.contrib.auth",
    "django.contrib.contenttypes",
    "django.contrib.sessions",
    "django.contrib.messages",
    "django.contrib.staticfiles",
    # Pradize apps
    "core",
    "stores",
    "media",
    "catalog",
    "discounts",
    "orders",
    "payments",
    "customers",
    "analytics",
    "aijobs",
    "permalinks",
    "cart",
    "emails",
    "shipping",
    "reviews",
    "campaigns",
    "sitemaps",
    # Static pages + contact/quotation forms (ADR-018, T029-SP)
    "pages",
    # Storefront engine (ADR-012, TICKET-029)
    "storefront",
    # Currency display converter (ADR-023, TICKET-031/TICKET-037)
    "currency",
    # Third-party pixel integrations (ADR-022, TICKET-030)
    "pixels",
    # GDPR/ePrivacy consent management (ADR-025, TICKET-048)
    "consent",
    # AI sales-assistant chat (ADR-024, TICKET-038)
    "chat",
    # Lead capture overlay + recent-purchase social proof (ADR-027, TICKET-034/035)
    "engagement",
    # Catalog feeds: Google Shopping + Facebook Dynamic Product Ads (ADR-026, TICKET-033)
    "feeds",
    # Security badge designer (ADR-030, TICKET-046)
    "badges",
]

MIDDLEWARE = [
    "django.middleware.security.SecurityMiddleware",
    "django.contrib.sessions.middleware.SessionMiddleware",
    "analytics.middleware.FirstTouchUTMMiddleware",
    "django.middleware.common.CommonMiddleware",
    "core.middleware.HostResolutionMiddleware",
    # ADR-008: locale middleware runs after legacy host resolution (settlement period —
    # both run in parallel; LocaleMiddleware overwrites request.store for storefront paths).
    "stores.middleware.LocaleMiddleware",
    # ADR-012: theme middleware runs after locale middleware (needs request.store set).
    # Resolves request.theme_ctx (ThemeContext) for storefront paths only.
    "storefront.middleware.ThemeMiddleware",
    "django.middleware.csrf.CsrfViewMiddleware",
    "django.contrib.auth.middleware.AuthenticationMiddleware",
    "django.contrib.messages.middleware.MessageMiddleware",
    "django.middleware.clickjacking.XFrameOptionsMiddleware",
]

ROOT_URLCONF = "webecom.urls"

TEMPLATES = [
    {
        "BACKEND": "django.template.backends.django.DjangoTemplates",
        "DIRS": [],
        "APP_DIRS": True,
        "OPTIONS": {
            "context_processors": [
                "django.template.context_processors.request",
                "django.contrib.auth.context_processors.auth",
                "django.contrib.messages.context_processors.messages",
                # ADR-012: exposes theme_ctx (ThemeContext) to all storefront templates.
                "storefront.context_processors.storefront_context",
            ],
            # ADR-023 §4/§6: currency_tags (display_price filter, currency_conversion_note,
            # currency_picker) available in every template with no {% load %} line — this
            # keeps price-line-only edits in templates owned by other tickets truly
            # single-line (no accompanying {% load %} edit required).
            "builtins": [
                "currency.templatetags.currency_tags",
            ],
        },
    },
]

WSGI_APPLICATION = "webecom.wsgi.application"


# Custom User model (ADR-001 §1)
AUTH_USER_MODEL = "core.User"

# All primary keys are BigAutoField (64-bit integers) for high-volume tables.
DEFAULT_AUTO_FIELD = "django.db.models.BigAutoField"


# Password validation

AUTH_PASSWORD_VALIDATORS = [
    {
        "NAME": "django.contrib.auth.password_validation.UserAttributeSimilarityValidator",
    },
    {
        "NAME": "django.contrib.auth.password_validation.MinimumLengthValidator",
    },
    {
        "NAME": "django.contrib.auth.password_validation.CommonPasswordValidator",
    },
    {
        "NAME": "django.contrib.auth.password_validation.NumericPasswordValidator",
    },
]


# Internationalization
#
# Storefront languages are NOT a static Django LANGUAGES tuple: each store declares
# its own languages dynamically via StoreLanguage.lang_code (stores/models.py,
# ADR-008) — any code, not restricted to a fixed choices list. Django's own
# LocaleMiddleware / Accept-Language negotiation is intentionally not used;
# stores.middleware.LocaleMiddleware resolves (store, domain, language) from the
# host + path instead. LOCALE_PATHS covers project-level / non-app-specific
# catalogs; per-app catalogs (e.g. storefront/locale/) are auto-discovered by
# Django because the app is in INSTALLED_APPS.
#
# ADR-021 wires the above into gettext: stores.middleware.LocaleMiddleware is the
# single translation.activate() call site for storefront requests (settings.
# LANGUAGE_CODE baseline on every request, then the resolved StoreLanguage);
# checkout views additionally activate the sf_lang-first checkout language via
# storefront/views_checkout.py's activate_checkout_language decorator.

LANGUAGE_CODE = "en-us"

LOCALE_PATHS = [BASE_DIR / "locale"]

TIME_ZONE = "UTC"

USE_I18N = True

USE_TZ = True


# Static files (CSS, JavaScript, Images)

STATIC_URL = "static/"


# Media files

MEDIA_URL = '/media/'
MEDIA_ROOT = os.path.join(BASE_DIR, 'media_files')  # local dev storage

# Storage backend — overridden in production.py for S3
DEFAULT_FILE_STORAGE = 'django.core.files.storage.FileSystemStorage'

# Image upload constraints
MAX_IMAGE_UPLOAD_MB = 10
ALLOWED_IMAGE_TYPES = ['image/jpeg', 'image/png', 'image/webp', 'image/gif']


# ---------------------------------------------------------------------------
# Cache
# ---------------------------------------------------------------------------

# Default: in-process memory cache — suitable for single-process development
# and CI.  In production, substitute Redis via the environment:
#
#   CACHES = {
#       "default": {
#           "BACKEND": "django.core.cache.backends.redis.RedisCache",
#           "LOCATION": os.environ["CACHE_URL"],
#       }
#   }
#
# This variable must be set in production.py (not here) so it picks up the
# CACHE_URL env var from the deployment environment.
CACHES = {
    "default": {
        "BACKEND": "django.core.cache.backends.locmem.LocMemCache",
    }
}


# Analytics — second database (ADR-004)
# The 'analytics' alias is added in each environment's settings file alongside 'default'.

DATABASE_ROUTERS = ['analytics.db_router.AnalyticsRouter']

# Raw analytics events are purged after this many days.
# Aggregated metrics (AggregatedMetric) are never purged.
ANALYTICS_EVENT_RETENTION_DAYS = 90

# Number of days shown in the analytics dashboard (TICKET-013).
ANALYTICS_REPORT_DAYS = 30

# W3-3 (WAVE3_AUDIT.md): maximum page_version_views/page_version_atc
# AggregatedMetric dimension rows kept per store per day (see
# analytics/management/commands/aggregate_metrics.py::PAGE_VERSION_TOP_N).
# Neither product_id nor page_version_id in the beacon payload is validated
# against the store's real catalog, so this cap is what actually bounds
# AggregatedMetric row growth for these two metric types. Raise for stores
# with unusually large versioned-product catalogs.
ANALYTICS_PAGE_VERSION_TOP_N = 500


# ADR-008: Platform subdomain apex domain. Required by the stores data migration.
# Example: "pradize.com" → "mystore.pradize.com"
# Must be set via environment variable.  Development settings pre-set it to "webecom.local".
_apex = os.environ.get("PLATFORM_APEX_DOMAIN")
if not _apex:
    raise ImproperlyConfigured(
        "PLATFORM_APEX_DOMAIN must be set in the environment. "
        "Set it to your platform's apex domain (e.g. 'webecom.io')."
    )
PLATFORM_APEX_DOMAIN = _apex


# ---------------------------------------------------------------------------
# Storefront (ADR-012, TICKET-029)
# ---------------------------------------------------------------------------

# The reference/platform-default theme package key (used as ultimate fallback in
# resolve_theme() when no Theme DB row is found).  Asserted by the storefront system
# check (storefront.E001) — deployment fails loudly if this is missing.
STOREFRONT_REFERENCE_THEME = "general"

# Preview token TTL in seconds (ADR-012 D8).  Hard-capped at ≤ 86400 (24 h).
STOREFRONT_PREVIEW_TTL = 7200  # 2 hours default


# Payment credential encryption key (ADR-006 §7, TICKET-017).
# Must be a URL-safe base64-encoded 32-byte key — separate from SECRET_KEY.
# Generate with: from cryptography.fernet import Fernet; print(Fernet.generate_key().decode())
# In production, provide via environment variable — never hardcode.
FERNET_KEY = os.environ.get("FERNET_KEY", "")


# ---------------------------------------------------------------------------
# Email
# ---------------------------------------------------------------------------

# EMAIL_BACKEND: console in development; override with SMTP in production.
EMAIL_BACKEND = os.environ.get(
    'EMAIL_BACKEND',
    'django.core.mail.backends.console.EmailBackend',
)

# DEFAULT_FROM_EMAIL: the platform sender address for all transactional emails.
# Stores can set a display name and reply-to only (DECIDED Settings-C1, TICKET-022).
# emails.sender.resolve_from_email(store) returns this for any store that has
# not configured (or not yet verified) a per-store SenderDomain (TICKET-040).
DEFAULT_FROM_EMAIL = os.environ.get('DEFAULT_FROM_EMAIL', 'noreply@example.com')

# --- Per-store custom sender domains (TICKET-040, ADR-034) -----------------

# Platform sender domain used in the generated SPF "include:" mechanism and as
# the "platform-DKIM alignment" signing domain referenced by SenderDomain's
# generated DNS records (emails/models.py). Deliberately separate from
# PLATFORM_APEX_DOMAIN — that setting is HTTP host routing; this is the
# outbound-mail domain — even though both may be the same literal string in
# production.
SENDER_DOMAIN_PLATFORM_HOST = os.environ.get('SENDER_DOMAIN_PLATFORM_HOST', 'mail.pradize.com')

# Timeout (seconds) applied to every dnspython TXT lookup during SenderDomain
# verification (emails/dns_verification.py). ADR-034 "Risks": DNS propagation
# itself is slow, but a single query must never hang a Celery worker — this
# bounds both dns.resolver.Resolver.timeout (per-query) and .lifetime (total).
SENDER_DOMAIN_DNS_TIMEOUT_SECONDS = int(os.environ.get('SENDER_DOMAIN_DNS_TIMEOUT_SECONDS', '5'))

# Kill switch (ADR-034 "Rollback strategy"): False makes resolve_from_email()
# always return DEFAULT_FROM_EMAIL, instantly reverting every store to the
# platform sender with no data migration and no impact on any other table.
SENDER_DOMAINS_ENABLED = os.environ.get('SENDER_DOMAINS_ENABLED', 'true').lower() == 'true'

# ADR-029 D6 (DECIDED, human, 2026-07-11): French law (prescription commerciale)
# effectively lets a customer claim stored value for ~5 years; a short gift-card
# expiry is unenforceable for stores whose markets include France. This is the
# platform-level floor discounts.validators.validate_gift_card_campaign enforces
# at publish time for FR-targeting stores (StoreLanguage/ShippingCountry basis,
# same detection ADR-027 D3 uses for its own DE-targeting check) — never a
# ceiling, and never applied to stores that do not target France. Configurable
# via env for legal review; default 1826 days (5 x 365 + 1 leap-year buffer).
GIFT_CARD_FR_MIN_VALIDITY_DAYS = int(
    os.environ.get('GIFT_CARD_FR_MIN_VALIDITY_DAYS', '1826')
)


# ---------------------------------------------------------------------------
# Celery
# ---------------------------------------------------------------------------

# Broker and result backend — override via environment variables in production.
CELERY_BROKER_URL = os.environ.get("CELERY_BROKER_URL", "redis://localhost:6379/0")
CELERY_RESULT_BACKEND = os.environ.get("CELERY_RESULT_BACKEND", "redis://localhost:6379/0")

# Serialization — JSON is safe and human-readable; never use pickle.
CELERY_TASK_SERIALIZER = "json"
CELERY_RESULT_SERIALIZER = "json"
CELERY_ACCEPT_CONTENT = ["json"]

# Timezone is already UTC via TIME_ZONE / USE_TZ above.
CELERY_TIMEZONE = "UTC"

# ---------------------------------------------------------------------------
# Checkout GC
# ---------------------------------------------------------------------------

# Void PENDING_PAYMENT orders whose CheckoutState is this old (hours).
# 24 h is long enough for abandoned-checkout recovery emails (ADR-010) to land
# on the retry view while short enough to not hold FIXED_QTY stock hostage for
# the full 7-day payment-intent authorization window (ADR-015 §3, Q-E ASSUMPTION).
CHECKOUT_STALE_HOURS = int(os.environ.get("CHECKOUT_STALE_HOURS", "24"))


# ---------------------------------------------------------------------------
# AI sales-assistant chat (ADR-024, TICKET-038)
# ---------------------------------------------------------------------------

# Global kill switch (ADR-024 Decision 7): when true, session minting and message
# handling are refused platform-wide with no deploy needed beyond an env change
# + restart (ADR-024 Rollback strategy). Env-driven, string "1"/"true" == on.
CHAT_KILL_SWITCH = os.environ.get("CHAT_KILL_SWITCH", "").strip().lower() in ("1", "true", "yes")

# Default model id (ADR-024 Decision 5). Per-store override via
# StoreChatSettings.model_id_override.
CHAT_MODEL_ID = os.environ.get("CHAT_MODEL_ID", "claude-haiku-4-5")

# Session caps (ADR-024 Decision 6 / AC-CHAT-09) — checked pre-flight on every
# /chat/message/ request; exceeding either refuses with the matching
# ChatEndedReason ('message_cap' / 'token_cap') stamped on the session.
CHAT_SESSION_MESSAGE_CAP = 30
CHAT_SESSION_TOKEN_CAP = 150_000

# Session inactivity TTL in seconds (ASSUMPTION — ADR-024 defines the 'expired'
# ChatEndedReason but not its exact duration; flagged for Architect/Safety
# confirmation). A session whose last_activity_at is older than this is refused
# with ChatEndedReason.EXPIRED rather than silently starting a new one.
CHAT_SESSION_TTL_SECONDS = int(os.environ.get("CHAT_SESSION_TTL_SECONDS", str(2 * 3600)))

# Retention purge window (ADR-024 Decision 10) — ChatSession/ChatMessage rows
# older than this are deleted by chat.tasks.purge_expired_chat_sessions.
CHAT_RETENTION_DAYS = int(os.environ.get("CHAT_RETENTION_DAYS", "30"))

# Reply length cap per Anthropic Messages API call (ADR-024 Decision 5).
CHAT_REPLY_MAX_TOKENS = 1024

# Manual tool-use loop cap per user turn (ADR-024 Decision 5 / AC-CHAT-15).
CHAT_MAX_TOOL_ROUNDTRIPS = 5

# Hard timeout (seconds) on each Anthropic Messages API call (security audit
# H1, ADR-024 "Risks -> WSGI worker occupancy under SSE"). Passed straight to
# `anthropic.Anthropic(timeout=...)` in chat/anthropic_client.py — without it
# the SDK's own ~600s default applies and a single slow/hung upstream
# connection can pin a gunicorn worker for the whole `/chat/message/` SSE
# reply. On timeout the turn ends with a generic user-visible error event
# (chat/anthropic_client.py stream_turn's except-block) and the session stays
# usable; it is NOT a hard hang. Keep gunicorn's own `--timeout` >= this value
# on the release checklist.
CHAT_API_TIMEOUT_SECONDS = int(os.environ.get("CHAT_API_TIMEOUT_SECONDS", "30"))


# ---------------------------------------------------------------------------
# Catalog feeds (ADR-026, TICKET-033)
# ---------------------------------------------------------------------------

# Debounced regeneration cadence (FEED-005 default: 30 minutes). Also used by
# FeedTarget.is_stale to compute the STALE banner threshold (2x this value).
FEEDS_REGENERATE_INTERVAL_MIN = int(os.environ.get("FEEDS_REGENERATE_INTERVAL_MIN", "30"))

# Directory feed files are written to (feeds/tasks.py::_feed_file_path) and
# served from (feeds/views.py — FileResponse takes any filesystem path, so the
# view needs no code change). Deliberately OUTSIDE MEDIA_ROOT: MEDIA_ROOT is
# published at MEDIA_URL='/media/' (Django's static serving helper in DEBUG,
# and the reverse proxy's /media/ mapping in production, since product images
# live there too) — a feed file living under MEDIA_ROOT would therefore be
# fetchable at /media/feeds/<store_id>/<provider>/<file>.xml with NO token,
# from any host, bypassing feeds/views.py's hmac token gate entirely (security
# audit F3, docs/security/FEEDS_ENGAGEMENT_AUDIT.md). FEEDS_ROOT is never
# wired into any URLconf or static-serving helper, so nothing outside
# feeds/tasks.py and feeds/views.py can expose it.
# Overridable via env for deployments that want feed files on a separate
# volume/mount. No migration needed: FeedTarget.file_path values pointing at
# the old MEDIA_ROOT/feeds/ location simply go stale (404/503 until the next
# beat tick) and are rewritten under FEEDS_ROOT the moment each target is next
# regenerated (regenerate_dirty_feeds runs every FEEDS_REGENERATE_INTERVAL_MIN
# minutes; rebuild_all_feeds marks everything dirty nightly regardless) —
# feed files are fully regenerable, never a data-loss concern.
FEEDS_ROOT = os.environ.get("FEEDS_ROOT", str(BASE_DIR / "feeds_files"))


# Beat schedule — periodic tasks run by `celery beat`.
CELERY_BEAT_SCHEDULE = {
    # Probe all active ProcessorAccounts every 5 minutes and update health status.
    # Task defined in payments/tasks.py (ADR-006-R §1.2.1, TICKET-020R).
    "payments-health-probe": {
        "task": "payments.probe_processor_health",
        "schedule": crontab(minute="*/5"),
    },
    # Send review-request emails for orders shipped >= review_request_delay_days ago.
    # Task defined in reviews/tasks.py (TICKET-023).
    "send-review-requests": {
        "task": "reviews.tasks.send_pending_review_requests",
        "schedule": crontab(hour=8, minute=0),  # daily at 08:00 UTC
    },
    # Nightly sitemap cache rebuild — safety net on top of event-driven updates (SM-020).
    # Task defined in sitemaps/tasks.py (TICKET-026).
    "rebuild-sitemap-nightly": {
        "task": "sitemaps.tasks.rebuild_sitemap_cache",
        "schedule": crontab(hour=3, minute=0),  # daily at 03:00 UTC
    },
    # Scan for abandoned checkouts every 5 minutes and enqueue email sends (TICKET-021).
    # Task defined in campaigns/tasks.py (ADR-010 Q4).
    # ±5 min precision is acceptable for hour-granularity send delays (ADR-010 Q4 risks).
    "scan-abandoned-checkouts": {
        "task": "campaigns.tasks.scan_abandoned_checkouts",
        "schedule": crontab(minute="*/5"),
    },
    # Upsell capture-window watchdog — every 2 minutes (TICKET-028, ADR-011 Q5).
    # Captures AUTHORIZED ORIGINAL charges past their capture window, reconciles
    # stuck CAPTURE_IN_PROGRESS charges, finalizes sessions, and alerts on breaches.
    # 2-minute cadence bounds worst-case capture lag at ~2 min while staying cheap
    # (one indexed scan).  Independent from scan-abandoned-checkouts: separate failure
    # domain, separate cadence.  Both tasks are idempotent — overlapping runs are safe.
    "upsell-capture-watchdog": {
        "task": "campaigns.tasks.capture_window_watchdog",
        "schedule": crontab(minute="*/2"),
    },
    # GC: void PENDING_PAYMENT orders whose CheckoutState has not advanced to
    # CONFIRMED within CHECKOUT_STALE_HOURS hours (ADR-015 §3).
    # Every 6 hours — precision is not critical (TTL is 24 h by default).
    "void-stale-pending-orders": {
        "task": "cart.tasks.void_stale_pending_orders",
        "schedule": crontab(hour="*/6"),
    },
    # Optional ECB daily reference-rate refresh (ADR-023 §1, TICKET-031/TICKET-037).
    # OFF by default: the task itself checks CurrencyConverterSettings.auto_refresh_enabled
    # and no-ops when disabled, so it is always safe to have this registered.
    "currency-rate-refresh": {
        "task": "currency.tasks.refresh_currency_rates",
        "schedule": crontab(hour=4, minute=0),  # daily at 04:00 UTC
    },
    # Purge ChatSession/ChatMessage rows older than CHAT_RETENTION_DAYS
    # (ADR-024 Decision 10, AC-CHAT-14). Daily is enough precision for a
    # 30-day-default retention window.
    "purge-expired-chat-sessions": {
        "task": "chat.tasks.purge_expired_chat_sessions",
        "schedule": crontab(hour=2, minute=30),  # daily at 02:30 UTC
    },
    # Purge ConsentRecord rows older than CONSENT_RECORD_RETENTION_DAYS
    # (ADR-025 D2/P4, 13 months rolling). Daily is enough precision for a
    # 395-day retention window.
    "purge-consent-records": {
        "task": "consent.tasks.purge_consent_records",
        "schedule": crontab(hour=2, minute=45),  # daily at 02:45 UTC
    },
    # Debounced catalog-feed regeneration (ADR-026 D3, FEED-005) — reconciles
    # the FeedTarget matrix and regenerates every dirty target.
    "regenerate-dirty-feeds": {
        "task": "feeds.tasks.regenerate_dirty_feeds",
        "schedule": crontab(minute=f"*/{FEEDS_REGENERATE_INTERVAL_MIN}"),
    },
    # Nightly full feed rebuild — safety net for mutation paths that bypass
    # signals (ADR-026 D3 §V, FEED-005).
    "rebuild-all-feeds-nightly": {
        "task": "feeds.tasks.rebuild_all_feeds",
        "schedule": crontab(hour=3, minute=30),  # daily at 03:30 UTC
    },
    # SenderDomain DNS re-check (TICKET-040, ADR-034 "Verification flow").
    # Every 2 hours while PENDING — approximates the ADR's "30 attempts over
    # 3 days" budget (emails/sender_domain_service.py:MAX_VERIFICATION_ATTEMPTS/
    # MAX_VERIFICATION_WINDOW); exact cadence is not specified in the ADR —
    # ASSUMPTION, ticket implementation detail, not a product decision.
    "recheck-pending-sender-domains": {
        "task": "emails.tasks.recheck_pending_sender_domains",
        "schedule": crontab(minute=0, hour="*/2"),
    },
    # Weekly re-check of VERIFIED SenderDomain rows — demotes to FAILED if DNS
    # records have disappeared (domain-takeover protection, ADR-034 "Risks").
    # Cadence flagged "PENDING product decision" in ADR-034; weekly is the
    # documented default from TICKET-040's blockers list — ASSUMPTION.
    "recheck-verified-sender-domains": {
        "task": "emails.tasks.recheck_verified_sender_domains",
        "schedule": crontab(day_of_week=1, hour=5, minute=0),  # Monday 05:00 UTC
    },
}
