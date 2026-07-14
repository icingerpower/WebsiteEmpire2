"""
pixels app models: Pixel (ADR-022 D2).

One Pixel row per (store, provider) configures a client-side marketing pixel.
Rendering is never done from this model — see pixels/registry.py (per-provider
snippet templates) and pixels/slot_provider.py (the single bridging
SlotProvider that queries this table).
"""

from django.core.exceptions import ValidationError
from django.db import models

from core.models import StoreOwnedModel


class PixelProviderChoices(models.TextChoices):
    """
    Frozen provider keys (ADR-022 D1). These values MUST equal both
    pixels.registry's registered provider keys and every FiredPixel.pixel_type
    value written for a pixel purchase claim (orders.models.FiredPixel) —
    pinned by pixels/tests/test_registry.py.
    """

    FACEBOOK = "facebook", "Meta / Facebook Pixel"
    GA = "ga", "Google Analytics 4"
    TIKTOK = "tiktok", "TikTok Pixel"
    SNAPCHAT = "snapchat", "Snapchat Pixel"
    PINTEREST = "pinterest", "Pinterest Tag"


class Pixel(StoreOwnedModel):
    """
    One installed pixel for one store×provider (ADR-022 D2).

    Invariants:
    - unique_together (store, provider): one ID per platform per store
      (spec 06 §9 "one ID per platform") — installing a second ID for the
      same provider requires uninstalling (is_active=False) the first, not
      a second row.
    - pixel_id is validated against the provider's strict regex in clean()
      (full path via the admin ModelForm) AND re-checked by save() itself
      for any other write path (PIXELS_AUDIT.md LOW-2), AND escaped again at
      render time (|escapejs in every provider template) — the ID is
      injected into an inline <script>, so validation is a security
      boundary, not cosmetics (ADR-022 D2/Security).
    - is_active is the install/uninstall toggle (admin-018 "Installed / Not
      Installed" badges). Uninstalling never deletes the row — audit trail.
    - config_json is reserved for v1 (future: CAPI token, consent category).
      No v1 code reads it.
    """

    provider = models.CharField(max_length=20, choices=PixelProviderChoices.choices)
    pixel_id = models.CharField(
        max_length=64,
        help_text="Provider-issued pixel/measurement ID. Format is validated per provider.",
    )
    is_active = models.BooleanField(
        default=True,
        help_text="Installed pixels only render while active. Deactivate to uninstall.",
    )
    config_json = models.JSONField(
        default=dict,
        blank=True,
        help_text="Reserved for future use (e.g. CAPI token, consent category). Unread in v1.",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "pixel"
        verbose_name_plural = "pixels"
        unique_together = [("store", "provider")]
        indexes = [
            models.Index(fields=["store", "is_active"]),
        ]

    def __str__(self):
        return f"{self.get_provider_display()} ({self.pixel_id}) — {self.store}"

    def save(self, *args, **kwargs):
        """
        Enforce the pixel_id regex on every write path (F1 lesson: validators
        are inert on .save() unless called explicitly — PIXELS_AUDIT.md
        LOW-2).

        Deliberately narrower than full_clean(): only re-checks pixel_id
        against the provider's id_pattern, and only when the provider key is
        still registered. It does NOT re-run clean()'s "unknown provider"
        rejection or validate_unique(), because:
        - a Pixel row whose provider plugin was later removed from the
          registry is an accepted, real state (see this model's class
          docstring and pixels/tests/test_slot_provider.py
          UnknownProviderKeyTest) — such a row must remain saveable (e.g. to
          flip is_active=False) even though a *new* row like it could never
          pass clean(); calling full_clean() here would make that impossible.
        - validate_unique() duplicates the DB-level unique_together
          constraint that callers already rely on raising IntegrityError.

        The admin ModelForm path already calls full_clean() (including the
        unknown-provider check and validate_unique()) before save() is
        reached, so admin writes keep both layers; this closes the gap for
        shell/fixture/future-API writes that bypass the admin form.
        """
        from pixels.registry import get_provider

        provider = get_provider(self.provider)
        if provider is not None and not provider.id_pattern.match(self.pixel_id or ""):
            raise ValidationError(
                {
                    "pixel_id": (
                        f"Invalid {provider.id_label}. Expected format: "
                        f"{provider.id_pattern.pattern!r}."
                    )
                }
            )
        super().save(*args, **kwargs)

    def clean(self):
        """
        Validate pixel_id against the provider's registered regex (ADR-022 D2).

        Rejects anything outside the strict charset — this is the first of
        two escaping layers before the value reaches an inline <script>
        (the second is |escapejs at render time, pixels/templates/pixels/).
        """
        from pixels.registry import get_provider

        provider = get_provider(self.provider)
        if provider is None:
            # A Pixel row can reference a provider key with no registry entry
            # only if a plugin was removed after rows were created. clean()
            # cannot happen for NEW rows (choices restricts the form field),
            # but stays defensive for direct .save() calls / fixtures.
            raise ValidationError(
                {"provider": f"Unknown pixel provider {self.provider!r} — no registry entry."}
            )
        if not provider.id_pattern.match(self.pixel_id or ""):
            raise ValidationError(
                {
                    "pixel_id": (
                        f"Invalid {provider.id_label}. Expected format: "
                        f"{provider.id_pattern.pattern!r}."
                    )
                }
            )
