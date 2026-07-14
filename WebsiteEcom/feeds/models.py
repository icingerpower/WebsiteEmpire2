"""
feeds app models: FeedConfig, StoreFeedToken, FeedTarget (ADR-026 D7, TICKET-033).

All additive — no changes to any existing table. Every model extends
StoreOwnedModel (ADR-001 §4); everything else the feature needs (the country
-> currency map, the provider registry) is code, not schema, matching
ADR-022's "platform data in code" precedent.

FeedConfig  [S] — one per (store, provider): enable toggle + products-to-sync
                  scope. The admin card (T033-B) binds here.
StoreFeedToken [S] — one per store, created lazily. The single secret token
                  required as ?token= on every feed URL (AC-213); rotation
                  replaces the value atomically and instantly revokes the old one.
FeedTarget  [S] — one per (store, provider, StoreLanguage, country_code): the
                  explicit generation/status state machine (FEED-005/§XV-3)
                  plus the persisted per-product exclusion audit trail
                  (AC-211). Rows are created/deleted by the reconciliation
                  step in feeds/tasks.py, never by hand.
"""

import logging
import secrets
from datetime import timedelta

from django.conf import settings
from django.db import models, transaction
from django.utils import timezone

from core.models import StoreOwnedModel

logger = logging.getLogger(__name__)


class FeedProviderChoices(models.TextChoices):
    """
    Frozen provider keys (ADR-026 D1). These values MUST equal both
    feeds.registry's registered provider keys and the URL segment
    (/feeds/<provider>/...) — pinned by feeds/tests/test_registry.py.
    """

    GOOGLE = "google", "Google Shopping"
    FACEBOOK = "facebook", "Facebook Dynamic Product Ads"


class FeedScope(models.TextChoices):
    """Products-to-sync scope selector (FEED-006, admin-013 "Products to sync")."""

    ALL = "all", "All products"
    COLLECTIONS = "collections", "Selected collections"


class FeedTargetStatus(models.TextChoices):
    """
    Explicit per-feed state machine (ADR-026 D3, §XV-3 "feed status is an
    explicit state enum, not decoration").

    PENDING      — never generated yet (reconciled into existence, beat hasn't
                   run for it). Served as 503 Retry-After.
    REGENERATING — a beat run has claimed this target and is building it now.
    UP_TO_DATE   — last generation succeeded; file_path is fresh and served.
    ERROR        — last generation failed (loud, §XV-1); the PREVIOUS good
                   file_path (if any) keeps being served — a bad generation
                   never blanks a live ad catalog.
    """

    PENDING = "pending", "Pending (never generated)"
    REGENERATING = "regenerating", "Regenerating"
    UP_TO_DATE = "up_to_date", "Up to date"
    ERROR = "error", "Error"


class FeedConfig(StoreOwnedModel):
    """
    One row per (store, provider) — the enable toggle + products-to-sync scope
    (ADR-026 D7/D8).

    is_enabled default False: a provider must be explicitly turned on from the
    admin card before any FeedTarget rows are reconciled into existence
    (feeds/tasks.py._reconcile_matrix).

    scope='collections' requires at least one row in `collections` — enforced
    by the owning FeedProvider.validate_settings() (feeds/registry.py), not a
    DB constraint (M2M can't be validated before the row has a PK).
    """

    provider = models.CharField(max_length=20, choices=FeedProviderChoices.choices)
    is_enabled = models.BooleanField(
        default=False,
        help_text="Provider must be explicitly enabled before feed targets are generated.",
    )
    scope = models.CharField(
        max_length=16,
        choices=FeedScope.choices,
        default=FeedScope.ALL,
    )
    collections = models.ManyToManyField(
        "catalog.Collection",
        blank=True,
        related_name="feed_configs",
        help_text="Only meaningful when scope='collections'. Empty = no products (misconfigured).",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "feed config"
        verbose_name_plural = "feed configs"
        unique_together = [("store", "provider")]

    def __str__(self):
        return f"{self.get_provider_display()} ({self.store_id})"


class StoreFeedToken(StoreOwnedModel):
    """
    One secret token per store (ADR-026 D2, AC-213) — required as ?token= on
    every feed URL for this store, regardless of provider ("the token is per-
    store" per the AC wording; per-provider tokens were considered and
    rejected).

    Created lazily via get_or_create_for_store(); rotate() replaces the value
    atomically so the old token stops working immediately (instant-revoke).

    Comparison at request time (feeds/views.py) uses hmac.compare_digest —
    never a plain == — to avoid a timing side-channel on the token value.
    """

    token = models.CharField(
        max_length=64,
        unique=True,
        help_text="secrets.token_urlsafe(32) — never logged, never displayed except in admin's own copy-URL UI.",
    )
    rotated_at = models.DateTimeField(default=timezone.now)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "store feed token"
        verbose_name_plural = "store feed tokens"
        constraints = [
            models.UniqueConstraint(fields=["store"], name="unique_feed_token_per_store"),
        ]

    def __str__(self):
        return f"feed token for store={self.store_id}"

    @classmethod
    def get_or_create_for_store(cls, store) -> "StoreFeedToken":
        """
        Return the store's single StoreFeedToken, creating it on first call.

        select_for_update() inside the atomic block means two concurrent
        callers racing to create the row are serialized by the DB row lock —
        neither creates a second row (mirrors CurrencyConverterSettings.get_solo()).
        """
        with transaction.atomic():
            obj, _created = cls.objects.for_store(store).select_for_update().get_or_create(
                store=store,
                defaults={"token": secrets.token_urlsafe(32)},
            )
        return obj

    def rotate(self) -> str:
        """
        Replace the token value atomically. Returns the new raw token.

        Uses a queryset .update() (not .save()) so the write is a single
        atomic statement; the in-memory instance is refreshed to match.

        Security audit F2: .update() does NOT fire post_save, so
        feeds/signals.py::_feed_token_created (which only ever runs on the
        initial lazy-creation .get_or_create()) never sees a rotation. This
        explicit log call is therefore the ONLY server-side audit record of a
        rotation — never remove it without replacing it. Never logs the
        token value itself, only the store id.
        """
        new_token = secrets.token_urlsafe(32)
        now = timezone.now()
        StoreFeedToken.objects.for_store(self.store).filter(pk=self.pk).update(
            token=new_token, rotated_at=now
        )
        self.token = new_token
        self.rotated_at = now
        logger.info("feeds: token rotated for store %s", self.store_id)
        return new_token


class FeedTarget(StoreOwnedModel):
    """
    One per (store, provider, StoreLanguage, country_code) — ADR-026 D2/D3/D7.

    Reconciled into existence by feeds/tasks.py._reconcile_matrix (never
    created by hand): a target exists iff its FeedConfig.is_enabled, its
    StoreLanguage.is_enabled, and its country_code is one of that
    StoreLanguage's ShippingCountry rows. Removing any of those three
    deletes the row (and its file) — the resulting 403/404 is the visible
    state, never a zombie file (ML-012).

    exclusions_json: list of {"reason": str, "product_id": int,
    "variant_id": int | None} — the persisted per-product exclusion audit
    trail required by AC-211 (never silent).
    """

    provider = models.CharField(max_length=20, choices=FeedProviderChoices.choices)
    store_language = models.ForeignKey(
        "stores.StoreLanguage",
        on_delete=models.PROTECT,
        related_name="feed_targets",
    )
    country_code = models.CharField(
        max_length=2,
        help_text="ISO 3166-1 alpha-2, uppercase — mirrors the parent ShippingCountry row.",
    )
    status = models.CharField(
        max_length=16,
        choices=FeedTargetStatus.choices,
        default=FeedTargetStatus.PENDING,
    )
    is_dirty = models.BooleanField(
        default=True,
        help_text="Set by signals on any dependency change; cleared at the start of each regeneration.",
    )
    last_generated_at = models.DateTimeField(null=True, blank=True)
    last_error = models.TextField(blank=True, default="")
    item_count = models.PositiveIntegerField(default=0)
    excluded_count = models.PositiveIntegerField(default=0)
    exclusions_json = models.JSONField(
        default=list,
        blank=True,
        help_text='[{"reason": "no_image", "product_id": 1, "variant_id": null}, ...] (AC-211).',
    )
    file_path = models.CharField(
        max_length=500,
        blank=True,
        default="",
        help_text=(
            "Absolute path under FEEDS_ROOT (never web-served — security audit F3). "
            "Empty until the first successful generation."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "feed target"
        verbose_name_plural = "feed targets"
        unique_together = [("store", "provider", "store_language", "country_code")]
        indexes = [
            models.Index(fields=["store", "is_dirty"]),
        ]

    def __str__(self):
        return f"{self.provider}:{self.country_code}-{self.store_language_id} ({self.status})"

    @property
    def is_stale(self) -> bool:
        """
        STALE banner condition (ADR-026 D3): dirty AND the last generation (or
        row creation, if never generated) is older than 2x the debounce
        interval — meaning a beat tick should already have picked it up and
        hasn't (worth surfacing to the admin as amber, distinct from ERROR).
        """
        if not self.is_dirty:
            return False
        interval_minutes = getattr(settings, "FEEDS_REGENERATE_INTERVAL_MIN", 30)
        threshold = timezone.now() - timedelta(minutes=interval_minutes * 2)
        reference = self.last_generated_at or self.updated_at
        return reference is not None and reference < threshold
