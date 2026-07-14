"""
Consent models (ADR-025 D2/D6, TICKET-048).

ConsentSettings — one row per store, lazily created with platform defaults
    (get_or_create_consent_settings() below) so a brand-new store is
    immediately covered without a migration data-population step.

ConsentRecord — one INSERT per shopper decision (accept_all/refuse_all/
    custom/withdraw); the server-side proof row correlated to the
    `pradize_consent` cookie via consent_id (demonstrability chain, ADR-025
    D2). Deliberately carries NO IP address field (see class docstring).
"""

from django.conf import settings
from django.db import models

from core.models import StoreOwnedModel


class ConsentAction(models.TextChoices):
    ACCEPT_ALL = "accept_all", "Accept all"
    REFUSE_ALL = "refuse_all", "Refuse all"
    CUSTOM = "custom", "Custom"
    WITHDRAW = "withdraw", "Withdraw"


class ConsentSource(models.TextChoices):
    BANNER = "banner", "Banner"
    MANAGE_PANEL = "manage_panel", "Manage panel"


class ConsentSettings(StoreOwnedModel):
    """
    Per-store consent feature configuration (ADR-025 D6).

    is_enabled defaults to True (safe-by-default rollout, P3 — DECIDED human
    2026-07-11: ALL stores get the banner at migration time). The ONLY path
    to is_enabled=False is the non_eu_acknowledged escape hatch, enforced in
    consent/admin.py's ConsentSettingsAdminForm — never cleared once set,
    exactly like chat's subprocessor_terms_accepted_at (StoreChatSettings).
    """

    is_enabled = models.BooleanField(default=True)
    non_eu_acknowledged = models.BooleanField(default=False)
    non_eu_acknowledged_at = models.DateTimeField(null=True, blank=True)
    non_eu_acknowledged_by = models.ForeignKey(
        settings.AUTH_USER_MODEL,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
    )
    # ADR-025 D4: per-store conservative override demoting the first-party
    # audience-measurement beacon into the "analytics" consent category.
    beacon_requires_consent = models.BooleanField(default=False)
    policy_page = models.ForeignKey(
        "pages.StaticPage",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
    )

    class Meta:
        verbose_name = "Consent settings"
        verbose_name_plural = "Consent settings"
        constraints = [
            # LOW-4c (routed spec-review finding, same latent bug as
            # engagement.SocialProofSettings): "one row per store" was only
            # ever true by convention (get_or_create_consent_settings below)
            # — nothing stopped a second row from being created directly.
            # Mirrors chat.StoreChatSettings' unique_store_chat_settings
            # constraint.
            models.UniqueConstraint(fields=["store"], name="unique_consent_settings_per_store"),
        ]

    def __str__(self):
        return f"Consent settings for store #{self.store_id}"


def get_or_create_consent_settings(store) -> ConsentSettings:
    """
    Return this store's ConsentSettings row, creating it with platform
    defaults on first access (ADR-025 D6: "one row per store, created lazily
    with defaults"). store=store is passed explicitly to get_or_create's
    create-path kwargs — QuerySet.get_or_create() only derives its INSERT
    params from the kwargs given to the call itself, not from the queryset's
    already-applied .filter(store=store) chain, so omitting it here would
    raise IntegrityError on the very first (uncreated) store.
    """
    obj, _created = ConsentSettings.objects.for_store(store).get_or_create(store=store)
    return obj


class ConsentRecord(StoreOwnedModel):
    """
    Server-side proof of one shopper consent decision (ADR-025 D2).

    No IP address field, deliberately: IP adds a PII retention burden and is
    not required for demonstrability — created_at + consent_id +
    policy_version + the chosen categories are sufficient proof of "what was
    shown, what was chosen, when, under which policy". Never add an IP field
    here without a fresh ADR (this is a pinned regression-test invariant,
    mirroring the analytics.Event "no IP" invariant from ADR-004/D4).

    Retention: rows older than CONSENT_RECORD_RETENTION_DAYS are purged by
    consent.tasks.purge_consent_records (P4, 13 months rolling).
    """

    consent_id = models.UUIDField(db_index=True)
    analytics = models.BooleanField(default=False)
    marketing = models.BooleanField(default=False)
    am_objected = models.BooleanField(default=False)
    action = models.CharField(max_length=16, choices=ConsentAction.choices)
    policy_version = models.PositiveSmallIntegerField()
    lang = models.CharField(max_length=8)
    source = models.CharField(max_length=16, choices=ConsentSource.choices)
    # Truncated at write time (consent/views.py) — proof context only, never
    # a durable per-shopper identifier.
    user_agent = models.CharField(max_length=256, blank=True, default="")
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "Consent record"
        verbose_name_plural = "Consent records"
        indexes = [
            # Composite index leading with store_id (core.models.StoreOwnedModel
            # convention) — the purge task and the audit-changelist both filter
            # on (store, created_at).
            models.Index(fields=["store", "created_at"], name="consent_rec_store_created_idx"),
        ]

    def __str__(self):
        return f"{self.action} {self.consent_id} @ {self.created_at:%Y-%m-%d %H:%M}"
