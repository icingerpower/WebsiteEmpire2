"""
Model-level validation for the discounts app (ADR-002, ADR-029 §XV-5: single
validation function per model, called from clean() on every save path).

validate_gift_card_campaign() is the only entry point for GiftCardCampaign
validation — the admin form's clean() and any programmatic .full_clean() call
both go through GiftCardCampaign.clean(), which delegates here.
"""

from datetime import timedelta

from django.conf import settings
from django.core.exceptions import ValidationError
from django.utils import timezone


def _store_targets_fr(store) -> bool:
    """
    True when `store` sells into / serves France (ADR-029 D6) — the same basis
    ADR-027 D3 uses for its own DE-targeting detection, mirrored for France:
    either condition alone is sufficient.

      1. An active StoreLanguage(lang_code='fr') row (the store literally
         serves a French storefront edition), OR
      2. A ShippingCountry(country_code='FR') row attached to an enabled
         StoreLanguage (the store ships to France from any enabled edition,
         even an English-only storefront that targets France as a market).
    """
    from stores.models import ShippingCountry, StoreLanguage

    if StoreLanguage.objects.filter(store=store, lang_code='fr', is_enabled=True).exists():
        return True

    return ShippingCountry.objects.filter(
        store_language__store=store,
        store_language__is_enabled=True,
        country_code='FR',
    ).exists()


def gift_card_campaign_fr_floor_violation(campaign, floor_days=None) -> bool:
    """
    True when `campaign`'s CURRENT expiry configuration would violate the FR
    5-year statutory floor (ADR-029 D6) for a store that targets France
    RIGHT NOW.

    Shared by two call sites so the floor cannot be bypassed by timing:
    - validate_gift_card_campaign() below, at publish time.
    - discounts.service.issue_campaign_reward_for_order(), at issuance time —
      re-evaluated against the store's CURRENT markets so a store that adds
      the FR market after a campaign was published (when the floor did not
      yet apply) stops issuing short-expiry cards immediately, rather than
      keeping the publish-time answer forever (wave-3 spec review).

    floor_days defaults to settings.GIFT_CARD_FR_MIN_VALIDITY_DAYS (1826 —
    5 years) when not passed explicitly.
    """
    if not campaign.store_id or not _store_targets_fr(campaign.store):
        return False

    if floor_days is None:
        floor_days = getattr(settings, 'GIFT_CARD_FR_MIN_VALIDITY_DAYS', 1826)

    from .models import GiftCardCampaignExpiryMode

    if campaign.expiry_mode == GiftCardCampaignExpiryMode.RELATIVE_DAYS:
        return bool(campaign.expiry_days) and campaign.expiry_days < floor_days
    if campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE:
        if not campaign.expiry_date:
            return False
        min_date = (timezone.now() + timedelta(days=floor_days)).date()
        return campaign.expiry_date < min_date
    return False


def validate_gift_card_campaign(campaign) -> None:
    """
    Validates a GiftCardCampaign. Called from GiftCardCampaign.clean().

    Per ADR-029 D1, the strict checks below are only enforced when publishing
    (status=PUBLISHED) — a draft may be saved incomplete, matching the "Save as
    draft" vs "Publish" screen affordance. Basic type/choice validation is
    already handled by Django's field choices.

    trigger_products (a ManyToMany) cannot be reliably inspected here for an
    unsaved instance going through a ModelForm (Django excludes m2m fields from
    Model.full_clean() during the normal ModelForm flow — they are validated by
    the form itself, see discounts/admin.py:GiftCardCampaignAdminForm.clean()).
    This function still checks trigger_products when campaign.pk is set (e.g. a
    saved instance re-validated in a test or a shell session) as defense in depth.
    """
    from .models import (
        GiftCardCampaignExpiryMode,
        GiftCardCampaignStatus,
        GiftCardCampaignTriggerScope,
        GiftCardCampaignValueMode,
    )

    errors = {}

    # Reserved, PENDING (human) value modes — never selectable/issuable in v1
    # regardless of draft/published (D3).
    if campaign.value_mode in (
        GiftCardCampaignValueMode.PERCENT_OF_ORDER,
        GiftCardCampaignValueMode.PERCENT_DISCOUNT_COUPON,
    ):
        errors['value_mode'] = (
            "This gift card value mode is reserved for a future release "
            "(ADR-029 D3, PENDING human decision) — only 'Set value' (fixed) "
            "is available in v1."
        )

    # Reserved trigger scope — mirrors the value_mode treatment above (§XV-1:
    # a published campaign must never silently never-fire).
    # discounts.service.campaign_matches_order() always returns False for
    # 'conditions', so a campaign left in this scope would publish
    # successfully yet never issue a single card with no error anywhere —
    # checked regardless of draft/published, same posture as D3.
    if campaign.trigger_scope == GiftCardCampaignTriggerScope.CONDITIONS:
        errors['trigger_scope'] = (
            "'Products based on conditions' is reserved for a future release "
            "(ADR-029 D2, PENDING human decision) — only 'All products' and "
            "'Manually add products' are available in v1."
        )

    # Structural expiry coherence — checked regardless of status so a campaign
    # is never left in a self-contradictory state (missing date for the mode
    # it claims to use).
    if campaign.expiry_mode == GiftCardCampaignExpiryMode.RELATIVE_DAYS:
        if campaign.expiry_days is not None and campaign.expiry_days <= 0:
            errors['expiry_days'] = "expiry_days must be a positive number of days."
    elif campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE:
        if not campaign.expiry_date:
            errors['expiry_date'] = "expiry_date is required when expiry_mode='absolute_date'."

    # Frequency cap coherence — checked regardless of status (an enabled cap
    # with a zero count/window is never meaningful).
    if campaign.cap_enabled:
        if not campaign.cap_count or campaign.cap_count <= 0:
            errors['cap_count'] = "cap_count must be greater than 0 when cap_enabled is set."
        if not campaign.cap_window_value or campaign.cap_window_value <= 0:
            errors['cap_window_value'] = (
                "cap_window_value must be greater than 0 when cap_enabled is set."
            )

    # The remaining checks are publish-time gates only (D1: "publishing requires...").
    if campaign.status == GiftCardCampaignStatus.PUBLISHED:
        if campaign.value_mode == GiftCardCampaignValueMode.FIXED and campaign.value <= 0:
            errors['value'] = "value must be greater than 0 to publish a fixed-value campaign."

        if (
            campaign.trigger_scope == GiftCardCampaignTriggerScope.MANUAL_PRODUCTS
            and campaign.pk
        ):
            # campaign.trigger_products.exists() would raise: catalog.Product is
            # a StoreOwnedModel, so the M2M related manager's get_queryset()
            # always returns the isolation-enforcing _RaisingQuerySet
            # (core/managers.py) regardless of relation filtering — the same
            # limitation documented in engagement/service.py:resolve_excluded_paths.
            # Query from the Product side via .for_store() instead.
            from catalog.models import Product

            has_products = Product.objects.for_store(campaign.store).filter(
                gift_card_campaigns=campaign
            ).exists()
            if not has_products:
                errors['trigger_products'] = (
                    "Select at least one product before publishing a "
                    "'Manually add products' campaign."
                )

        if (
            campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE
            and campaign.expiry_date
            and campaign.expiry_date < timezone.now().date()
        ):
            errors['expiry_date'] = "expiry_date cannot be in the past for a published campaign."

        # D6 — FR statutory floor (DECIDED, human, 2026-07-11). The boolean
        # check itself lives in gift_card_campaign_fr_floor_violation() so the
        # issuance-time re-check in discounts.service shares the exact same
        # rule (wave-3 spec review) — only the field-specific error message is
        # built here, at publish time.
        if gift_card_campaign_fr_floor_violation(campaign):
            floor_days = getattr(settings, 'GIFT_CARD_FR_MIN_VALIDITY_DAYS', 1826)
            if campaign.expiry_mode == GiftCardCampaignExpiryMode.RELATIVE_DAYS:
                errors['expiry_days'] = (
                    f"This store targets France — gift card validity must be at "
                    f"least {floor_days} days (prescription commerciale, ADR-029 D6)."
                )
            elif campaign.expiry_mode == GiftCardCampaignExpiryMode.ABSOLUTE_DATE:
                errors['expiry_date'] = (
                    f"This store targets France — the expiry date must be at "
                    f"least {floor_days} days from now (ADR-029 D6)."
                )

    if errors:
        raise ValidationError(errors)
