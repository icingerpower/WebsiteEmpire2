"""
Engagement models: lead-capture overlay campaigns (TICKET-034) and social-proof
settings (TICKET-035) — ADR-027.

LeadCaptureCampaign / LeadCaptureCampaignTranslation / LeadSignup implement the
campaign-shaped lead capture described in ADR-027 D2/D3. SocialProofSettings
implements the per-store singleton described in ADR-027 D8.

Counters (visitors_count/impressions_count/conversions_count) are ONLY ever
updated via F() expressions (engagement/service.py, engagement/views.py) — never
`obj.count += 1; obj.save()` (same discipline as DiscountCode.times_used,
ADR-002 §4).
"""

from django.conf import settings
from django.core.exceptions import ValidationError
from django.core.validators import MaxValueValidator, MinValueValidator
from django.db import models

from core.models import StoreOwnedModel
from engagement.themes import DEFAULT_OVERLAY_THEME_KEY, OVERLAY_THEMES

# ADR-027 D6: the "legal flag". Named social-proof display modes (first_name,
# first_name_city) exist in the schema and the admin form as gated enum values
# but are PENDING human/legal sign-off (same track as the ADR-025 beacon item).
# Until this flips to True:
#   - the admin form only ever offers 'anonymous' as a selectable choice
#     (engagement/admin.py SocialProofSettingsAdminForm), and
#   - the feed builder (engagement/service.py) renders in anonymous mode no
#     matter what is stored on display_mode (defense in depth — fail closed,
#     mirrors the consent app's fail-closed conventions).
# Flipping this is a one-line change + a fresh Safety review before enabling
# named modes for any store; it must NOT be flipped by this ticket.
NAMED_SOCIAL_PROOF_MODES_ENABLED = False


# ---------------------------------------------------------------------------
# T034 — Lead capture overlay campaigns
# ---------------------------------------------------------------------------


class LeadCaptureTrigger(models.TextChoices):
    EXIT = "exit", "Exit"
    TIME = "time", "Time"


class MobileLeadCaptureTrigger(models.TextChoices):
    DISABLED = "disabled", "Disabled"
    TIME = "time", "Time"


class PostSignupAction(models.TextChoices):
    DISPLAY_MESSAGE = "display_message", "Display message"
    GO_TO_URL = "go_to_url", "Go to Url"


class CapWindowUnit(models.TextChoices):
    MINUTES = "minutes", "Minutes"
    HOURS = "hours", "Hours"
    DAYS = "days", "Days"


class LeadCaptureCampaign(StoreOwnedModel):
    """
    A lead-capture overlay campaign (ADR-027 D2, admin-015).

    Rendering selects, among active campaigns for a store, exactly one — the
    most recently created (engagement/service.py select_active_campaign) —
    per the AF-013 recommendation for concurrent campaigns.

    reward_discount_code links an EXISTING discounts.DiscountCode (v1 —
    per-lead generated unique codes are a later enhancement, NOT in this
    ticket). Issuance is idempotent via discounts.CampaignReward with
    campaign_id=f"lead_capture:{pk}" (engagement/service.py issue_reward).
    """

    name = models.CharField(max_length=255)
    is_active = models.BooleanField(default=True)

    trigger = models.CharField(
        max_length=10,
        choices=LeadCaptureTrigger.choices,
        default=LeadCaptureTrigger.EXIT,
    )
    trigger_delay_seconds = models.PositiveIntegerField(
        default=15,
        help_text="Used when trigger='time'. ASSUMPTION (ADR-027 D2): screenshot hides "
        "this input until 'Time' is selected.",
    )
    mobile_trigger = models.CharField(
        max_length=10,
        choices=MobileLeadCaptureTrigger.choices,
        default=MobileLeadCaptureTrigger.DISABLED,
    )
    mobile_trigger_delay_seconds = models.PositiveIntegerField(default=15)

    excluded_pages = models.ManyToManyField(
        "permalinks.Permalink",
        blank=True,
        related_name="excluding_lead_capture_campaigns",
        help_text="Pages that suppress this overlay for a visitor who browsed them. "
        "Resolved to a path list (all active languages of the same page) at render "
        "time — never store raw path strings (ADR-027 D2/D4).",
    )

    overlay_theme_key = models.CharField(
        max_length=50,
        default=DEFAULT_OVERLAY_THEME_KEY,
        help_text="Key into engagement.themes.OVERLAY_THEMES.",
    )

    headline = models.CharField(max_length=255, default="SIGN-UP FOR OUR NEWSLETTER")
    body = models.CharField(max_length=500, default="AND GET A 10% OFF COUPON", blank=True)
    cta_label = models.CharField(max_length=100, default="GET MY 10% OFF NOW")
    dismiss_label = models.CharField(
        max_length=150, default="I don't want to save money", blank=True
    )
    success_message = models.TextField(
        default="Thanks for signing up! Here is your code: {code}",
        help_text="Supports a {code} placeholder, substituted server-side "
        "(ADR-027 D3 step 6). Ignored when no reward_discount_code is configured "
        "(the {code} token is simply left blank).",
        blank=True,
    )

    post_signup_action = models.CharField(
        max_length=20,
        choices=PostSignupAction.choices,
        default=PostSignupAction.DISPLAY_MESSAGE,
    )
    redirect_url = models.URLField(blank=True, default="")

    cap_enabled = models.BooleanField(default=False)
    cap_impressions = models.PositiveIntegerField(default=1)
    cap_window_value = models.PositiveIntegerField(default=1)
    cap_window_unit = models.CharField(
        max_length=10, choices=CapWindowUnit.choices, default=CapWindowUnit.MINUTES
    )

    signup_tags = models.JSONField(
        default=list,
        blank=True,
        help_text="List of string tags appended to Customer.tags on signup "
        "(TICKET-011 tags contract).",
    )

    reward_discount_code = models.ForeignKey(
        "discounts.DiscountCode",
        null=True,
        blank=True,
        on_delete=models.PROTECT,
        related_name="lead_capture_campaigns",
        help_text="Existing coupon/gift-card code issued to every signup "
        "(shared code — abuse bounded at redemption by per_email_limit, ADR-002 §4).",
    )

    # Denormalized counters (ADR-027 D5) — ONLY ever updated via F() expressions.
    visitors_count = models.PositiveIntegerField(default=0)
    impressions_count = models.PositiveIntegerField(default=0)
    conversions_count = models.PositiveIntegerField(default=0)

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "lead capture campaign"
        verbose_name_plural = "lead capture campaigns"
        indexes = [
            models.Index(fields=["store", "is_active", "-created_at"], name="engagement_lcc_active_idx"),
        ]

    def __str__(self):
        return self.name

    @property
    def conversion_rate(self):
        """
        conversions / impressions, computed at read time, never stored
        (ADR-027 D5). Returns 0 when impressions_count is 0 (avoid ZeroDivisionError
        on a freshly created campaign with no traffic yet).
        """
        if not self.impressions_count:
            return 0
        return self.conversions_count / self.impressions_count

    def clean(self):
        errors = {}
        if self.overlay_theme_key not in OVERLAY_THEMES:
            errors["overlay_theme_key"] = (
                f"Unknown overlay theme key {self.overlay_theme_key!r}. "
                f"Valid keys: {sorted(OVERLAY_THEMES)}."
            )
        if self.post_signup_action == PostSignupAction.GO_TO_URL and not self.redirect_url:
            errors["redirect_url"] = "redirect_url is required when post_signup_action is 'go_to_url'."
        if self.reward_discount_code_id:
            # LOW-4b (routed spec-review finding): a reward code shared across
            # every signup of this campaign must be one issued FOR lead
            # capture (admin-013/ADR-026 audit trail) — never a merchant's
            # general-purpose coupon or a campaign/sold code borrowed here by
            # mistake. Defense in depth beyond the admin form's restricted FK
            # queryset (engagement/admin.py) — any direct .save() (tests,
            # shell) is also blocked, loud not silent (§XV-1).
            from discounts.models import ProvenanceType

            if self.reward_discount_code.provenance != ProvenanceType.LEAD_CAPTURE:
                errors["reward_discount_code"] = (
                    "reward_discount_code must have "
                    f"provenance={ProvenanceType.LEAD_CAPTURE!r} — got "
                    f"{self.reward_discount_code.provenance!r}."
                )
        if errors:
            raise ValidationError(errors)


class LeadCaptureCampaignTranslation(StoreOwnedModel):
    """
    Translated merchant-authored content for a LeadCaptureCampaign (ADR-027 D2/D9).

    Mirrors campaigns.CampaignStepTranslation exactly: draft -> published
    lifecycle, produced by the AiJob CLI runner (never direct API — memory:
    feedback_translation_cli). Resolution = published translation for the
    request language, else the campaign's base fields (engagement/service.py
    resolve_campaign_content).
    """

    STATUS_DRAFT = "draft"
    STATUS_PUBLISHED = "published"
    STATUS_CHOICES = [
        (STATUS_DRAFT, "Draft"),
        (STATUS_PUBLISHED, "Published"),
    ]

    campaign = models.ForeignKey(
        LeadCaptureCampaign,
        on_delete=models.CASCADE,
        related_name="translations",
    )
    lang_code = models.CharField(max_length=10)

    headline = models.CharField(max_length=255, blank=True)
    body = models.CharField(max_length=500, blank=True)
    cta_label = models.CharField(max_length=100, blank=True)
    dismiss_label = models.CharField(max_length=150, blank=True)
    success_message = models.TextField(blank=True)

    status = models.CharField(max_length=20, choices=STATUS_CHOICES, default=STATUS_DRAFT)
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="lead_capture_campaign_translations",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "lead capture campaign translation"
        verbose_name_plural = "lead capture campaign translations"
        unique_together = [("store", "campaign", "lang_code")]

    def __str__(self):
        return f"{self.campaign.name} [{self.lang_code}] ({self.status})"

    def clean(self):
        if self.store_id and self.campaign_id and self.store_id != self.campaign.store_id:
            raise ValidationError(
                "Translation store must match the campaign's store."
            )


class LeadSignup(StoreOwnedModel):
    """
    One row per successful signup (ADR-027 D2/D3) — the marketing-consent proof
    record (Art. 7(1) GDPR: who consented, when, via what).

    Deliberately NO IP address field — same invariant as consent.ConsentRecord
    and analytics.Event (ADR-025 D2 / ADR-004). IP is used transiently only in
    the antispam cache key (pages/antispam.py), never persisted here.

    confirmed_at is the double-opt-in fallback field described in ADR-027 D3
    ("the fallback is additive: a confirmed_at field on LeadSignup + one
    confirmation email template + one signed-token confirm view; no redesign").
    It is populated by engagement/views.py confirm_view once the shopper clicks
    the confirmation link sent for stores selling into Germany
    (engagement.service.is_de_targeting_store — human decision 2026-07-11: a
    store is DE-targeting when it has an active StoreLanguage(lang_code='de')
    row OR ships to Germany from any enabled storefront edition). Non-DE
    stores never populate confirmed_at (single opt-in, ADR-027 D3 default).
    """

    campaign = models.ForeignKey(
        LeadCaptureCampaign,
        on_delete=models.PROTECT,
        related_name="signups",
        help_text="PROTECT — the signup is the audit/consent-proof trail.",
    )
    email = models.EmailField()
    customer = models.ForeignKey(
        "customers.Customer",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="lead_signups",
    )
    lang = models.CharField(max_length=10, help_text="Language the overlay was shown in.")
    confirmed_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when the double opt-in confirmation link is clicked "
        "(DE-targeting stores only). Null for single opt-in signups.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "lead signup"
        verbose_name_plural = "lead signups"
        unique_together = [("store", "campaign", "email")]
        indexes = [
            models.Index(fields=["store", "campaign", "email"], name="engagement_signup_idx"),
        ]

    def save(self, *args, **kwargs):
        self.email = self.email.lower().strip()
        super().save(*args, **kwargs)

    def __str__(self):
        return f"{self.email} -> {self.campaign.name}"


# ---------------------------------------------------------------------------
# T035 — Social proof
# ---------------------------------------------------------------------------


class SocialProofDisplayMode(models.TextChoices):
    ANONYMOUS = "anonymous", "Anonymous"
    FIRST_NAME = "first_name", "First name"
    FIRST_NAME_CITY = "first_name_city", "First name + city"


class SocialProofPosition(models.TextChoices):
    BOTTOM_LEFT = "bottom_left", "Bottom left"
    BOTTOM_RIGHT = "bottom_right", "Bottom right"


class SocialProofSettings(StoreOwnedModel):
    """
    Per-store singleton for the recent-purchase social-proof widget
    (ADR-027 D6/D7/D8). Mirrors consent.ConsentSettings +
    get_or_create_consent_settings (see get_or_create_social_proof_settings
    below).

    is_enabled defaults to False (ticket blocker: not enabled by default
    before human sign-off, even for the anonymous-only mode shipped in v1).
    """

    is_enabled = models.BooleanField(default=False)
    display_mode = models.CharField(
        max_length=20,
        choices=SocialProofDisplayMode.choices,
        default=SocialProofDisplayMode.ANONYMOUS,
    )
    window_hours = models.PositiveIntegerField(
        default=1, validators=[MinValueValidator(1), MaxValueValidator(168)]
    )
    first_delay_seconds = models.PositiveIntegerField(
        default=2, validators=[MinValueValidator(1), MaxValueValidator(300)]
    )
    interval_seconds = models.PositiveIntegerField(
        default=2, validators=[MinValueValidator(1), MaxValueValidator(300)]
    )
    max_items = models.PositiveIntegerField(
        default=10, validators=[MinValueValidator(1), MaxValueValidator(20)]
    )
    position = models.CharField(
        max_length=20,
        choices=SocialProofPosition.choices,
        default=SocialProofPosition.BOTTOM_LEFT,
    )

    class Meta:
        verbose_name = "social proof settings"
        verbose_name_plural = "social proof settings"
        constraints = [
            # LOW-4c (routed spec-review finding): "per-store singleton" was
            # only ever true by convention (get_or_create_social_proof_settings
            # below) — nothing stopped a second row from being created
            # directly (tests, shell, a future call site). Mirrors
            # chat.StoreChatSettings' unique_store_chat_settings constraint.
            models.UniqueConstraint(fields=["store"], name="unique_social_proof_settings_per_store"),
        ]

    def __str__(self):
        return f"Social proof settings for store #{self.store_id}"

    def clean(self):
        """
        ADR-027 D6: named display modes are gated behind
        NAMED_SOCIAL_PROOF_MODES_ENABLED until human/legal sign-off. Defense in
        depth beyond the admin form's restricted choice list (engagement/admin.py)
        — any direct .save() (tests, shell, future code) is also blocked, loud
        not silent (§XV-1).
        """
        if (
            not NAMED_SOCIAL_PROOF_MODES_ENABLED
            and self.display_mode != SocialProofDisplayMode.ANONYMOUS
        ):
            raise ValidationError(
                {
                    "display_mode": (
                        "Named social-proof modes are pending human/legal sign-off "
                        "(ADR-027 D6) — only 'anonymous' may be selected until "
                        "NAMED_SOCIAL_PROOF_MODES_ENABLED is flipped."
                    )
                }
            )


def get_or_create_social_proof_settings(store) -> SocialProofSettings:
    """
    Return this store's SocialProofSettings row, creating it with platform
    defaults on first access (mirrors consent.get_or_create_consent_settings).
    """
    obj, _created = SocialProofSettings.objects.for_store(store).get_or_create(store=store)
    return obj
