"""
Campaign admin registrations for Pradize (TICKET-027, ADR-007, ADR-009, TICKET-021, ADR-010,
TICKET-039).

Registered on both admin sites (store_admin_site / super_admin_site).

Store admin:
  - CampaignAdmin: list, search, filter; get_queryset scoped via .for_store().
    Platform rows (owner_scope='platform') visible but read-only:
    has_change_permission and has_delete_permission return False.
    owner_scope is NOT exposed in the form — forced to 'store' in save_model.
    entry_step exposed via raw_id_fields.
    Activation gate: validate_campaign_activation called before super().save_model().
    Inlines: CampaignStepInline (funnel-type) OR AbandonedCheckoutEmailStepInline
    (abandoned_checkout type) — shown based on campaign_type (get_inline_instances).
  - CampaignStepInline: StackedInline on Campaign; store FK set automatically in
    save_formset. Read-only when parent campaign.owner_scope == 'platform'.
    get_formset() injects max(position)+10 as default for new rows (FR-B4 / T039).
  - AbandonedCheckoutEmailStepInline: StackedInline on Campaign; only shown when
    campaign_type == 'abandoned_checkout'. Read-only for platform campaigns.
  - CampaignStepAdmin: separate admin for CampaignStep with
    CampaignStepTranslationInline for managing translations. Uses structured
    CampaignStepAdminForm (FR-S1/S2/S3) and campaign-scoped branch dropdowns
    (FR-B5). Delete view warns about nulled branch references (FR-B8 / T039).
  - CampaignSessionAdmin: view-only (state machine transitions are code-driven).
  - OrderChargeAdmin: view-only (charges are created by the payment webhook).
  - CampaignIssuedCodeAdmin: view-only audit table (no add/change/delete).

Super admin:
  - CampaignSuperAdmin: cross-store via .cross_store_unsafe(). owner_scope defaults
    to 'platform' on new objects. Full CRUD on both scopes.
  - CampaignSessionSuperAdmin: view-only, cross-store.
  - OrderChargeSuperAdmin: view-only, cross-store.
  - CampaignSessionTokenSuperAdmin: super-admin-only, view-only.
  - CampaignIssuedCodeSuperAdmin: view-only audit table, cross-store.

T039 additions:
  - CampaignStepAdminForm: replaces raw offer_config_json with structured fields
    (title, description, cta_label, upsell_amount_cents, discount_pct,
    expires_in_days). offer_config_json is rebuilt in save() from these fields,
    preserving any existing archetype-specific keys.
  - _CampaignStepAdminMixin: shared delete_view override (FR-B8) and
    formfield_for_foreignkey helpers shared by both step admin classes.
  - Activation flow (FR-V2): two-step — first POST renders activate_confirm.html
    with validation results; second POST (confirm=1) actually saves.
  - set_entry_step_view: POST endpoint to set a step as campaign.entry_step (FR-B6).
"""

from django import forms
from django.contrib import admin, messages
from django.core.exceptions import ValidationError
from django.db.models import Max
from django.urls import path, reverse
from django.utils.translation import gettext_lazy as _

from campaigns.models import (
    AbandonedCheckoutEmailStep,
    Campaign,
    CampaignIssuedCode,
    CampaignSession,
    CampaignSessionToken,
    CampaignStep,
    CampaignStepTranslation,
    NON_TERMINAL_STATES,
    StepOfferType,
)
from core.admin import StoreOwnedInlineMixin
from core.admin_widgets import StoreScopedForeignKeyRawIdWidget
from orders.models import OrderCharge
from stores.models import StoreLanguage
from stores.permissions import check_module_access
from webecom.admin import store_admin_site, super_admin_site

#: ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): campaigns admins serve BOTH the "Up-sell
#: campaigns" and "Abandoned campaigns" rows — access is granted if EITHER
#: module allows it (check_module_access's tuple/ANY-of semantics).
CAMPAIGNS_MODULE_KEY = ("upsell_campaigns", "abandoned_campaigns")


# ---------------------------------------------------------------------------
# AbandonedCheckoutEmailStep form — hours/days input (ADR-010 Q2)
# ---------------------------------------------------------------------------

class AbandonedCheckoutEmailStepForm(forms.ModelForm):
    """
    Custom ModelForm for AbandonedCheckoutEmailStep that replaces the raw
    send_delay_hours integer with a user-friendly delay_value + delay_unit pair.

    The "Send after" field accepts an integer; the "Unit" dropdown converts it
    to hours before saving (days × 24 = hours). On edit, existing hours values
    are decomposed back into the most natural unit (whole-day multiples → days,
    otherwise hours).

    send_delay_hours is excluded from the rendered form and set programmatically
    in save() from cleaned_data["send_delay_hours"] (computed in clean()).
    """

    UNIT_HOURS = "hours"
    UNIT_DAYS = "days"
    UNIT_CHOICES = [(UNIT_HOURS, "hours"), (UNIT_DAYS, "days")]

    delay_value = forms.IntegerField(min_value=1, label="Send after")
    delay_unit = forms.ChoiceField(
        choices=UNIT_CHOICES, initial=UNIT_HOURS, label="Unit"
    )

    class Meta:
        model = AbandonedCheckoutEmailStep
        fields = "__all__"
        exclude = ["send_delay_hours"]

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Pre-populate delay_value / delay_unit from the stored send_delay_hours.
        if self.instance and self.instance.pk:
            h = self.instance.send_delay_hours
            if h % 24 == 0 and h >= 24:
                self.fields["delay_value"].initial = h // 24
                self.fields["delay_unit"].initial = self.UNIT_DAYS
            else:
                self.fields["delay_value"].initial = h
                self.fields["delay_unit"].initial = self.UNIT_HOURS

    def clean(self):
        cleaned = super().clean()
        value = cleaned.get("delay_value")
        unit = cleaned.get("delay_unit")
        if value is not None and unit:
            cleaned["send_delay_hours"] = (
                value * 24 if unit == self.UNIT_DAYS else value
            )
        return cleaned

    def save(self, commit=True):
        instance = super().save(commit=False)
        instance.send_delay_hours = self.cleaned_data.get("send_delay_hours", 1)
        if commit:
            instance.save()
        return instance


# ---------------------------------------------------------------------------
# CampaignStep structured form (FR-S1 / FR-S2 / FR-S3 — T039)
# ---------------------------------------------------------------------------

class CampaignStepAdminForm(forms.ModelForm):
    """
    Custom ModelForm for CampaignStep that replaces offer_config_json with
    structured fields (FR-S1/S2/S3 — T039).

    Common display fields (title, description, cta_label — FR-S2) and
    archetype-specific charge/coupon fields (upsell_amount_cents — FR-S3;
    discount_pct, expires_in_days for storewide_discount — FR-S1) are
    exposed as first-class form fields.

    On save(), these are merged into offer_config_json, preserving any existing
    archetype-specific keys not covered by this form (e.g. product_id,
    product_ids, buy_quantity, get_quantity). This allows the inline on the
    Campaign page (which still shows raw offer_config_json) to coexist with the
    standalone step editor.

    offer_config_json is excluded from the rendered form to prevent raw JSON
    editing on the store admin surface. Super admins see it as readonly via
    CampaignStepSuperAdmin.readonly_fields.
    """

    step_title = forms.CharField(
        max_length=200,
        required=False,
        label=_("Title"),
        help_text=_(
            "Customer-visible offer title "
            "(stored in offer_config_json['title'])."
        ),
    )
    step_description = forms.CharField(
        widget=forms.Textarea(attrs={"rows": 3}),
        required=False,
        label=_("Description"),
        help_text=_("Customer-visible offer description."),
    )
    cta_label = forms.CharField(
        max_length=100,
        required=False,
        label=_("CTA label"),
        help_text=_("Call-to-action button text shown to the shopper."),
    )
    upsell_amount_cents = forms.IntegerField(
        min_value=0,
        required=False,
        label=_("Charge amount (cents)"),
        help_text=_(
            "Amount charged for off-session upsell, in cents "
            "(e.g. 999 = $9.99). Required for all archetypes except "
            "storewide_discount."
        ),
    )
    discount_pct = forms.IntegerField(
        min_value=1,
        max_value=100,
        required=False,
        label=_("Discount percent"),
        help_text=_("Percentage discount for storewide_discount steps (1–100)."),
    )
    expires_in_days = forms.IntegerField(
        min_value=1,
        required=False,
        label=_("Coupon expires after (days)"),
        help_text=_("Days until the coupon expires. Default 30."),
    )

    class Meta:
        model = CampaignStep
        exclude = ["offer_config_json"]

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        # Pre-populate structured fields from the existing offer_config_json.
        if self.instance.pk and self.instance.offer_config_json:
            cfg = self.instance.offer_config_json
            self.fields["step_title"].initial = cfg.get("title", "")
            self.fields["step_description"].initial = cfg.get("description", "")
            self.fields["cta_label"].initial = cfg.get("cta_label", "")
            self.fields["upsell_amount_cents"].initial = cfg.get("upsell_amount_cents")
            self.fields["discount_pct"].initial = cfg.get("discount_pct")
            self.fields["expires_in_days"].initial = cfg.get("expires_in_days")

    def save(self, commit=True):
        instance = super().save(commit=False)
        # Start from the existing JSON so that archetype-specific keys
        # (product_id, product_ids, etc.) are preserved.
        cfg = dict(instance.offer_config_json or {})

        # Merge string content fields (only update when non-empty).
        for json_key, form_key in (
            ("title", "step_title"),
            ("description", "step_description"),
            ("cta_label", "cta_label"),
        ):
            val = self.cleaned_data.get(form_key)
            if val:
                cfg[json_key] = val

        # Merge integer fields (only update when explicitly provided).
        for json_key, form_key in (
            ("upsell_amount_cents", "upsell_amount_cents"),
            ("discount_pct", "discount_pct"),
            ("expires_in_days", "expires_in_days"),
        ):
            val = self.cleaned_data.get(form_key)
            if val is not None:
                cfg[json_key] = val

        instance.offer_config_json = cfg
        if commit:
            instance.save()
            self.save_m2m()
        return instance


# ---------------------------------------------------------------------------
# Shared step-admin mixin (FR-B5 helpers + FR-B8 delete_view — T039)
# ---------------------------------------------------------------------------

class _CampaignStepAdminMixin:
    """
    Mixin shared by CampaignStepAdmin (store) and CampaignStepSuperAdmin.

    Provides:
    - _get_current_step_id(): extract step pk from the URL resolver match.
    - _get_campaign_id_from_request(): look up campaign_id for the step being
      edited (used by formfield_for_foreignkey to scope branch dropdowns).
    - delete_view() override: prepend a warning listing steps whose
      accept_next_step / decline_next_step FKs will be SET to NULL on deletion
      (FR-B8 — T039).
    """

    def _get_current_step_id(self, request):
        """
        Extract the step pk from the URL resolver match.

        Django sets request.resolver_match on every view routed through the URL
        conf.  For the step change view the 'object_id' kwarg holds the step pk.
        Returns None for the add view or if resolver_match is not available.
        """
        if hasattr(request, 'resolver_match') and request.resolver_match:
            return request.resolver_match.kwargs.get('object_id')
        return None

    def _get_campaign_id_from_request(self, request):
        """
        Get the campaign_id for the CampaignStep currently being edited.

        Returns None for the add view (no step_id in URL) or if the step does
        not exist.  The queryset uses cross_store_unsafe() so that both the
        store-admin and super-admin surfaces can look up the campaign FK without
        hitting an IsolationError.
        """
        step_id = self._get_current_step_id(request)
        if step_id:
            try:
                return (
                    CampaignStep.objects.cross_store_unsafe()
                    .values_list('campaign_id', flat=True)
                    .get(pk=int(step_id))
                )
            except (CampaignStep.DoesNotExist, ValueError, TypeError):
                return None
        return None

    def get_readonly_fields(self, request, obj=None):
        """
        Lock price-bearing fields when the campaign is active (M3 secondary guard).

        The primary guard is CampaignSessionToken.offered_amount_cents checked in
        accept_upsell — it rejects accepts when the price changed after token issue.
        This secondary guard surfaces the risk on the admin form: when a campaign
        is active, the fields that determine what will be charged are made readonly
        so an admin cannot accidentally alter the price while live sessions are
        in progress. It provides UX feedback rather than server-side enforcement.

        Fields locked (only those that exist on CampaignStepAdminForm or the model):
        - upsell_amount_cents — structured form field for the direct charge amount
        - discount_pct        — structured form field for storewide-discount steps
        - offer_config_json   — raw JSON field (already readonly on super-admin;
                                excluded from store-admin form, so harmless there)
        """
        readonly = list(super().get_readonly_fields(request, obj))
        if obj is not None and obj.campaign.is_active:
            for field in ("offer_config_json", "upsell_amount_cents", "discount_pct"):
                if field not in readonly:
                    readonly.append(field)
        return readonly

    @admin.display(description=_("Translations"))
    def translation_status_html(self, obj):
        """
        FR-S5: Per-language translation status matrix for a CampaignStep.

        For each StoreLanguage configured on obj.store, renders one badge:
        - Green ✓ when a CampaignStepTranslation exists for that lang_code
          with a non-empty title field.
        - Grey – when the translation is absent or its title is blank.

        Returns a mark_safe HTML string assembled entirely from format_html()
        calls — no raw user data is injected unescaped.
        """
        from django.utils.html import format_html, mark_safe

        lang_codes = list(
            StoreLanguage.objects.filter(store=obj.store)
            .values_list("lang_code", flat=True)
        )
        if not lang_codes:
            return "—"

        translated = set(
            CampaignStepTranslation.objects.for_store(obj.store)
            .filter(step=obj, title__gt="")
            .values_list("lang_code", flat=True)
        )

        parts = []
        for lang_code in lang_codes:
            if lang_code in translated:
                parts.append(format_html(
                    '<span style="color:#2c7a2c;font-weight:bold">{} ✓</span>',
                    lang_code.upper(),
                ))
            else:
                parts.append(format_html(
                    '<span style="color:#aaa">{} –</span>',
                    lang_code.upper(),
                ))

        return mark_safe("&nbsp;&nbsp;".join(str(p) for p in parts))

    def delete_view(self, request, object_id, extra_context=None):
        """
        Extend the default delete confirmation to list sibling steps that
        will have their accept_next_step / decline_next_step FK set to NULL
        on deletion (ON DELETE SET_NULL semantics — FR-B8 / T039).

        The warning is injected as extra_context and rendered by the custom
        campaignstep/delete_confirmation.html template.  The actual delete
        logic is unchanged (super().delete_view handles it).
        """
        extra_context = dict(extra_context or {})
        try:
            step = CampaignStep.objects.cross_store_unsafe().get(pk=object_id)
            nulled_accept = list(
                CampaignStep.objects.cross_store_unsafe()
                .filter(accept_next_step_id=step.pk)
                .select_related('campaign')
            )
            nulled_decline = list(
                CampaignStep.objects.cross_store_unsafe()
                .filter(decline_next_step_id=step.pk)
                .select_related('campaign')
            )
            extra_context['nulled_accept_refs'] = nulled_accept
            extra_context['nulled_decline_refs'] = nulled_decline
            extra_context['has_nulled_refs'] = bool(nulled_accept or nulled_decline)
        except CampaignStep.DoesNotExist:
            pass
        return super().delete_view(request, object_id, extra_context=extra_context)


# ---------------------------------------------------------------------------
# Shared inlines
# ---------------------------------------------------------------------------

class CampaignStepTranslationInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Inline for CampaignStepTranslation rows within the CampaignStep change page.

    Translations are produced by the AiJob CLI system. The ai_job FK is
    read-only — it is set programmatically by the signal handler, never
    entered manually. The store FK is excluded; it is set in save_formset.

    StoreOwnedInlineMixin's get_queryset() -> cross_store_unsafe() fixes
    CAMPAIGN-STEP-INLINE-ISOLATION (BUG_TESTS.csv): without it, Django's own
    InlineModelAdmin.get_queryset() default uses CampaignStepTranslation's
    raising StoreScopedManager, 500ing on ANY GET/POST of a CampaignStep's
    own change form (which is where CampaignStepAdmin's
    raw_id_fields = ["campaign"] fix needed to be exercised over real HTTP).
    Its StoreSafeInlineFormSet also fixes INLINE-FORMSET-PK-ISOLATION for
    POSTing an existing translation row.
    """

    model = CampaignStepTranslation
    extra = 0
    fields = ["lang_code", "title", "description", "cta_label", "status", "ai_job"]
    readonly_fields = ["ai_job"]
    exclude = ["store"]


class CampaignStepInline(StoreOwnedInlineMixin, admin.StackedInline):
    """
    Inline for CampaignStep rows within the Campaign change page.

    The `store` FK is excluded from the form because it is always set to match
    the parent Campaign.store in CampaignAdmin.save_formset / save_model.
    The self-referential accept_next_step / decline_next_step fields use
    raw_id_fields to avoid loading all steps into a dropdown.

    FR-B4 (T039): get_formset() injects max(position)+10 as the default
    value for the position field on new (extra) inline rows so that clicking
    "Add step" pre-fills a sensible non-conflicting position.

    Only shown for funnel-type campaigns (not abandoned_checkout).
    """

    model = CampaignStep
    extra = 0
    fields = [
        "position",
        "offer_type",
        "offer_config_json",
        "accept_next_step",
        "decline_next_step",
    ]
    raw_id_fields = ["accept_next_step", "decline_next_step"]
    # store is set programmatically in save_formset; exclude it from the form.
    exclude = ["store"]

    # get_queryset() -> cross_store_unsafe() (fixing CAMPAIGN-STEP-INLINE-
    # ISOLATION, BUG_TESTS.csv) and the StoreSafeInlineFormSet (fixing
    # INLINE-FORMSET-PK-ISOLATION) both come from StoreOwnedInlineMixin.
    # get_formset() below wraps StoreOwnedInlineMixin's formset via
    # super().get_formset() — _FormsetWithDefaultPosition therefore also
    # re-parents onto StoreSafeInlineFormSet automatically (it subclasses
    # whatever super().get_formset() returns, which Django's
    # inlineformset_factory always builds as a subclass of self.formset).

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        accept_next_step / decline_next_step both target CampaignStep
        (StoreOwnedModel); use the safe raw-id widget so
        label_and_url_for_value() doesn't crash via the raising default
        manager the instant this inline re-renders with a bound value
        (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py). Does not change
        the fields' queryset/selection scoping.
        """
        if db_field.name in ("accept_next_step", "decline_next_step"):
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def get_formset(self, request, obj=None, **kwargs):
        """
        Return a formset class whose extra (new) forms default position to
        max(existing_positions) + 10 (FR-B4 — T039).

        When no steps exist yet, the first new row defaults to position 10.
        The override only affects unbound extra forms (displayed on GET);
        for POST submissions the submitted value takes precedence normally.
        """
        FormsetClass = super().get_formset(request, obj, **kwargs)
        if obj is None:
            return FormsetClass

        # .for_store(obj.store) — not .filter() on the bare manager, which
        # would return the raising StoreScopedManager's queryset. .aggregate()
        # does NOT actually raise on that raising queryset (a gap in
        # core/managers.py._RaisingQuerySet found while writing the
        # RAW-ID-WIDGET-ISOLATION regression tests — .aggregate() bypasses
        # __iter__/__len__/__bool__/_fetch_all, so it silently executes an
        # unscoped query instead of raising IsolationError; flagged
        # separately for core/managers.py, not fixed here — this call site
        # only needed its own scoping fixed).
        agg = CampaignStep.objects.for_store(obj.store).filter(campaign=obj).aggregate(Max('position'))
        max_pos = agg['position__max']
        _next_pos = (max_pos + 10) if max_pos is not None else 10

        class _FormsetWithDefaultPosition(FormsetClass):
            def add_fields(self, form, index):
                super().add_fields(form, index)
                # index is None for the JS "empty form" template (rendered
                # via formset.empty_form, e.g. every time this formset's
                # .media is accessed) — CAMPAIGN-STEP-INLINE-ISOLATION
                # (BUG_TESTS.csv) found this crashing with a bare TypeError
                # (None >= int) on every Campaign change-form GET, unrelated
                # to isolation. Only real (non-empty-form) rows get the
                # default position.
                if (
                    index is not None
                    and index >= self.initial_form_count()
                    and 'position' in form.fields
                ):
                    form.fields['position'].initial = _next_pos

        return _FormsetWithDefaultPosition

    def has_add_permission(self, request, obj=None):
        """Prevent adding steps to a platform campaign from the store admin."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_add_permission(request, obj)

    def has_change_permission(self, request, obj=None):
        """
        Prevent editing steps of a platform campaign from the store admin.

        Django passes the parent Campaign as `obj` to inline permission hooks
        when editing a campaign — not the CampaignStep. So `obj` IS the
        Campaign here; check obj.owner_scope directly.
        """
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_change_permission(request, obj)

    def has_delete_permission(self, request, obj=None):
        """
        Prevent deleting steps from a platform campaign via the store admin.

        See has_change_permission: obj IS the parent Campaign, not a step.
        """
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_delete_permission(request, obj)


class AbandonedCheckoutEmailStepInline(StoreOwnedInlineMixin, admin.StackedInline):
    """
    Inline for AbandonedCheckoutEmailStep rows within the Campaign change page.

    Only shown when campaign_type == 'abandoned_checkout' (enforced in
    CampaignAdmin.get_inline_instances / CampaignSuperAdmin.get_inline_instances).

    Platform campaign rows are read-only for store-admins (ADR-009 §2 rules).
    The store FK is excluded; it is set programmatically in save_formset.

    send_delay_hours is not exposed directly — the form presents delay_value +
    delay_unit (hours/days) and converts to hours × 24 on save (ADR-010 Q2).
    """

    model = AbandonedCheckoutEmailStep
    form = AbandonedCheckoutEmailStepForm
    extra = 0
    fields = [
        "position",
        "delay_value",
        "delay_unit",
        "email_type",
        "email_style",
        "subject",
        "body_template",
        "coupon_config_json",
    ]
    exclude = ["store"]

    # get_queryset() -> cross_store_unsafe() (fixing CAMPAIGN-STEP-INLINE-
    # ISOLATION, same bug class as CampaignStepInline, for the
    # abandoned_checkout campaign_type branch) and the StoreSafeInlineFormSet
    # both come from StoreOwnedInlineMixin.

    def has_add_permission(self, request, obj=None):
        """Prevent adding email steps to a platform campaign from the store admin."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_add_permission(request, obj)

    def has_change_permission(self, request, obj=None):
        """Platform campaign email steps are read-only for store admins (ADR-009 §2)."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_change_permission(request, obj)

    def has_delete_permission(self, request, obj=None):
        """Platform campaign email steps cannot be deleted by store admins (ADR-009 §2)."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return super().has_delete_permission(request, obj)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _warn_out_of_order_delays(request, model_admin, campaign):
    """
    Emit a non-blocking WARNING if AbandonedCheckoutEmailStep delays for the given
    campaign are not strictly increasing by position.

    Does not reject the save — it is purely informational (ADR-010 Q2 note).
    Called from save_formset after AbandonedCheckoutEmailStep rows are persisted.

    ADR-031 addendum audit (TICKET-051): this call previously ran on the raw
    `.objects.filter(...)` manager — already a latent bug even before the
    terminal-method closure (list(...) triggers __iter__, which has always
    raised IsolationError on _RaisingQuerySet), meaning this warning silently
    500'd the entire admin save whenever it fired. Fixed by scoping to the
    parent campaign's store.
    """
    steps = list(
        AbandonedCheckoutEmailStep.objects.for_store(campaign.store)
        .filter(campaign=campaign)
        .order_by("position", "send_delay_hours")
        .values_list("send_delay_hours", flat=True)
    )
    for i in range(1, len(steps)):
        if steps[i] <= steps[i - 1]:
            model_admin.message_user(
                request,
                "Warning: email step delays are not strictly increasing — "
                "some emails may fire out of the intended order.",
                level=messages.WARNING,
            )
            break


def _build_funnel_tree_nodes(campaign, step_change_url_fn=None, set_entry_url_fn=None):
    """
    Walk the funnel graph from entry_step via gray/black DFS and build a
    flat list of render-ready node dicts for the admin funnel map template.

    step_change_url_fn: callable (pk -> URL string) for building step change links.
      Pass None to omit URLs (e.g. in tests).
    set_entry_url_fn: callable (step_pk -> URL string) for "Make entry" POST
      action links (FR-B6 — T039). Pass None to omit (e.g. in tests).

    Returns a dict:
      entries         — list of node dicts (DFS pre-order)
      unreachable     — list of dicts {step, label, step_url} not reachable from entry_step
      max_accept_depth — depth on the all-accepts path
      has_cycle       — bool
      warnings        — list of warning strings
    """
    from campaigns.validators import validate_offer_config
    from django.core.exceptions import ValidationError as DjValidationError

    result = {
        'entries': [],
        'unreachable': [],
        'max_accept_depth': 0,
        'has_cycle': False,
        'warnings': [],
    }

    if not campaign.entry_step_id:
        return result

    # Load all campaign steps in one query to avoid N+1 on branch FKs.
    # cross_store_unsafe() is safe here: the campaign has already been permission-checked
    # by the calling admin view (for_store / cross_store_unsafe at the view layer).
    all_steps = {}
    for s in CampaignStep.objects.cross_store_unsafe().filter(campaign=campaign).select_related(
        'accept_next_step', 'decline_next_step'
    ):
        all_steps[s.pk] = s

    entry = all_steps.get(campaign.entry_step_id)
    if entry is None:
        return result  # Dangling entry_step FK — step was deleted.

    visited = set()   # Fully rendered (black) — DFS exit marker.
    on_stack = set()  # Currently on the DFS path (gray) — cycle detection.

    def _step_label(step):
        """Build human-readable label: '#pk — offer_type_display — title — amount'."""
        if step is None:
            return ''
        cfg = step.offer_config_json or {}
        try:
            offer_display = step.get_offer_type_display()
        except AttributeError:
            offer_display = step.offer_type
        parts = [f'#{step.pk}', offer_display]
        title = cfg.get('title', '')
        if title:
            parts.append(f'"{title}"')
        amount = cfg.get('upsell_amount_cents')
        if isinstance(amount, int) and amount > 0:
            parts.append(f'{amount / 100:.2f}')
        return ' — '.join(parts)

    def _config_error_str(step):
        try:
            validate_offer_config(step.offer_type, step.offer_config_json or {})
            return None
        except DjValidationError as exc:
            return exc.message if hasattr(exc, 'message') else str(exc)

    def _branch_info(next_step_id):
        """Return branch display info dict for accept or decline edge."""
        if next_step_id is None:
            return {'step': None, 'label': '', 'is_terminal': True, 'step_url': None}
        next_step = all_steps.get(next_step_id)
        if next_step is None:
            return {
                'step': None,
                'label': f'(deleted step #{next_step_id})',
                'is_terminal': True,
                'step_url': None,
            }
        return {
            'step': next_step,
            'label': _step_label(next_step),
            'is_terminal': False,
            'step_url': step_change_url_fn(next_step.pk) if step_change_url_fn else None,
        }

    def _dfs(step, depth, accept_depth):
        """Recursive DFS pre-order: emit node then recurse into branches."""
        if step.pk in on_stack:
            # Back edge — cycle detected; emit a compact cycle marker.
            result['has_cycle'] = True
            result['entries'].append({
                'step': step,
                'label': _step_label(step),
                'depth': depth,
                'is_entry': step.pk == campaign.entry_step_id,
                'is_terminal': False,
                'config_error': None,
                'accept_info': None,
                'decline_info': None,
                'is_back_ref': False,
                'is_cycle': True,
                'step_url': step_change_url_fn(step.pk) if step_change_url_fn else None,
                'set_entry_url': set_entry_url_fn(step.pk) if set_entry_url_fn else None,
            })
            return

        if step.pk in visited:
            # Fan-in / diamond: already fully rendered — emit a reference only.
            result['entries'].append({
                'step': step,
                'label': _step_label(step),
                'depth': depth,
                'is_entry': step.pk == campaign.entry_step_id,
                'is_terminal': False,
                'config_error': None,
                'accept_info': None,
                'decline_info': None,
                'is_back_ref': True,
                'is_cycle': False,
                'step_url': step_change_url_fn(step.pk) if step_change_url_fn else None,
                'set_entry_url': set_entry_url_fn(step.pk) if set_entry_url_fn else None,
            })
            return

        on_stack.add(step.pk)
        visited.add(step.pk)

        if accept_depth > result['max_accept_depth']:
            result['max_accept_depth'] = accept_depth

        is_terminal = (
            step.accept_next_step_id is None
            and step.decline_next_step_id is None
        )

        result['entries'].append({
            'step': step,
            'label': _step_label(step),
            'depth': depth,
            'is_entry': step.pk == campaign.entry_step_id,
            'is_terminal': is_terminal,
            'config_error': _config_error_str(step),
            'accept_info': _branch_info(step.accept_next_step_id),
            'decline_info': _branch_info(step.decline_next_step_id),
            'is_back_ref': False,
            'is_cycle': False,
            'step_url': step_change_url_fn(step.pk) if step_change_url_fn else None,
            'set_entry_url': set_entry_url_fn(step.pk) if set_entry_url_fn else None,
        })

        # Recurse accept branch first (consistent DFS order).
        if step.accept_next_step_id is not None:
            next_accept = all_steps.get(step.accept_next_step_id)
            if next_accept:
                _dfs(next_accept, depth + 1, accept_depth + 1)

        # Recurse decline branch; decline path resets the accept-depth counter
        # (only the all-accepts path depth matters for the FR-V1 warning).
        if step.decline_next_step_id is not None:
            next_decline = all_steps.get(step.decline_next_step_id)
            if next_decline:
                _dfs(next_decline, depth + 1, 0)

        on_stack.discard(step.pk)

    _dfs(entry, 0, 0)

    # Unreachable: steps in all_steps but not reached by DFS.
    result['unreachable'] = [
        {
            'step': s,
            'label': _step_label(s),
            'step_url': step_change_url_fn(s.pk) if step_change_url_fn else None,
            'set_entry_url': set_entry_url_fn(s.pk) if set_entry_url_fn else None,
        }
        for pk, s in all_steps.items() if pk not in visited
    ]

    # Depth warning (FR-V1 non-blocking).
    # max_accept_depth is 0-indexed: a 4-step chain has max_accept_depth=3.
    # Warn when there are more than 3 steps on the all-accepts path (i.e. depth >= 3).
    if result['max_accept_depth'] >= 3:
        result['warnings'].append(
            str(_(
                "The all-accepts path is %(steps)s steps deep. "
                "Shoppers rarely accept more than 2–3 consecutive offers."
            ) % {'steps': result['max_accept_depth'] + 1})
        )
    if result['has_cycle']:
        result['warnings'].append(
            str(_("A cycle was detected in the funnel graph — activation will be blocked."))
        )

    return result


# ---------------------------------------------------------------------------
# _build_chain_preview — FR-V3 (T039)
# ---------------------------------------------------------------------------

def _build_chain_preview(campaign):
    """
    Pure simulation of the all-accepts and all-declines paths through the funnel.

    Returns a dict:
      has_entry               — bool; False when campaign.entry_step is not set
      accept_chain            — list of step dicts, one per step on the all-accepts path
      decline_chain           — list of step dicts, one per step on the all-declines path
      total_accept_revenue_cents — cumulative revenue if every offer is accepted

    Each accept_chain dict:
      step                    — CampaignStep instance
      offer_type              — step.offer_type value string
      amount_cents            — upsell_amount_cents from offer_config_json (0 if absent)
      discount_pct            — discount_pct from offer_config_json (None if absent)
      cumulative_revenue_cents — running total of amounts added so far
      is_terminal             — True when accept_next_step_id is None

    Each decline_chain dict:
      step                    — CampaignStep instance
      offer_type              — step.offer_type value string
      is_terminal             — True when decline_next_step_id is None

    Cycle-safe: a 'seen' set of step PKs breaks infinite loops when the funnel
    graph contains back-edges. The first visit is recorded; subsequent visits
    to the same node terminate the traversal.

    No DB writes. No external calls. DB reads only (FK traversal per step).
    """
    if not campaign.entry_step_id:
        return {"accept_chain": [], "decline_chain": [], "has_entry": False}

    accept_chain = []
    decline_chain = []

    # All-accepts path: follow accept_next_step from entry until terminal or cycle.
    step = campaign.entry_step
    seen = set()
    cumulative_revenue = 0
    while step and step.pk not in seen:
        seen.add(step.pk)
        cfg = step.offer_config_json or {}
        amount = cfg.get("upsell_amount_cents", 0)
        offer_type = step.offer_type
        # storewide_discount offers issue a coupon, not a direct charge — skip revenue.
        if offer_type != StepOfferType.STOREWIDE_DISCOUNT:
            cumulative_revenue += amount
        accept_chain.append({
            "step": step,
            "offer_type": offer_type,
            "amount_cents": amount,
            "discount_pct": cfg.get("discount_pct"),
            "cumulative_revenue_cents": cumulative_revenue,
            "is_terminal": step.accept_next_step_id is None,
        })
        step = step.accept_next_step

    # All-declines path: follow decline_next_step from entry until terminal or cycle.
    step = campaign.entry_step
    seen = set()
    while step and step.pk not in seen:
        seen.add(step.pk)
        decline_chain.append({
            "step": step,
            "offer_type": step.offer_type,
            "is_terminal": step.decline_next_step_id is None,
        })
        step = step.decline_next_step

    return {
        "has_entry": True,
        "accept_chain": accept_chain,
        "decline_chain": decline_chain,
        "total_accept_revenue_cents": cumulative_revenue,
    }


# ---------------------------------------------------------------------------
# Store admin
# ---------------------------------------------------------------------------

@admin.register(Campaign, site=store_admin_site)
class CampaignAdmin(admin.ModelAdmin):
    """
    Per-store Campaign admin. Queryset is scoped to the current store.

    Platform campaigns (owner_scope='platform') are visible to store admins
    for transparency but cannot be modified or deleted. owner_scope is NOT
    included in the form — it is forced to 'store' in save_model to prevent
    spoofing via POST (ADR-009 §2 permission enforcement).

    Activation gate: validate_campaign_activation() is called before saving
    when is_active=True. ValidationError is surfaced as an admin message.

    Inlines: only the inline relevant to the campaign_type is shown:
    - Funnel-type: CampaignStepInline
    - abandoned_checkout: AbandonedCheckoutEmailStepInline
    On creation (no existing obj), no inlines are shown — save first, then add steps.

    T039 additions:
    - activate_campaign_view: two-step confirmation flow (FR-V2).
    - set_entry_step_view: POST endpoint to designate an entry step (FR-B6).
    - render_change_form: passes set_entry_url_fn to _build_funnel_tree_nodes so
      that "Make entry" buttons appear in the funnel map.

    module_key: ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6) — CAMPAIGNS_MODULE_KEY (ANY-of
    upsell_campaigns/abandoned_campaigns). has_change_permission/
    has_delete_permission are overridden below rather than left to the
    generic mapping so the platform-read-only rule composes with the module
    check: previously these called super().has_change_permission(request,
    obj), which — since is_staff/is_superuser are never set by any Pradize
    flow (ADR-033 D6) — reached Django's stock, Permission-row-based check
    and returned False for every real employee (only test users with
    is_superuser=True forced through it); this was a latent bug pre-dating
    this ticket, now fixed by routing through check_module_access instead.
    """

    module_key = CAMPAIGNS_MODULE_KEY

    list_display = ["name", "campaign_type", "owner_scope", "is_active", "store"]
    list_filter = ["campaign_type", "owner_scope", "is_active"]
    search_fields = ["name"]
    raw_id_fields = ["entry_step"]
    # owner_scope is NOT editable from the store-admin form — forced to 'store' in
    # save_model. Exposing it in the form would allow a store-admin to spoof 'platform'.
    exclude = ["owner_scope"]

    def get_inline_instances(self, request, obj=None):
        """
        Show only the inline relevant to campaign_type.

        On a new campaign (obj=None and no POST campaign_type), no inlines are
        shown — the user must save the campaign first, then add steps.
        """
        campaign_type = None
        if obj is not None:
            campaign_type = obj.campaign_type
        elif request.method == "POST":
            campaign_type = request.POST.get("campaign_type")

        instances = []
        if campaign_type == "abandoned_checkout":
            instances.append(AbandonedCheckoutEmailStepInline(self.model, self.admin_site))
        elif campaign_type is not None:
            instances.append(CampaignStepInline(self.model, self.admin_site))
        return instances

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return Campaign.objects.for_store(request.store)
        return Campaign.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        Scope entry_step FK to the current store so raw_id_fields widget validation
        uses a store-filtered queryset instead of the raising StoreScopedManager.
        Without this, ModelChoiceField.clean() triggers IsolationError (500) on submit.

        Also uses the safe raw-id widget (RAW-ID-WIDGET-ISOLATION,
        core/admin_widgets.py): entry_step targets CampaignStep
        (StoreOwnedModel), so the stock ForeignKeyRawIdWidget would 500 in
        label_and_url_for_value() the instant this field re-renders with a
        bound value (e.g. any other validation error on the same form).
        """
        if db_field.name == "entry_step":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
            if getattr(request, "store", None):
                kwargs["queryset"] = CampaignStep.objects.for_store(request.store)
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def has_change_permission(self, request, obj=None):
        """Platform campaigns are read-only for store admins (ADR-009 §2)."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return check_module_access(request, self.module_key, "full")

    def has_delete_permission(self, request, obj=None):
        """Platform campaigns cannot be deleted by store admins (ADR-009 §2)."""
        if obj is not None and obj.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return check_module_access(request, self.module_key, "full")

    def save_model(self, request, obj, form, change):
        """
        Set store on new Campaign instances; force owner_scope='store'; run
        activation gate validation.
        """
        if not change and getattr(request, "store", None):
            obj.store = request.store

        # Force owner_scope to 'store' regardless of POST data — store admins
        # cannot create platform campaigns (ADR-009 §2 permission enforcement).
        obj.owner_scope = Campaign.OWNER_SCOPE_STORE

        # Activation gate: validate before saving. ValidationError is surfaced
        # as an admin message so the save is aborted cleanly.
        try:
            from campaigns.validators import validate_campaign_activation
            validate_campaign_activation(obj)
        except ValidationError as exc:
            msgs = exc.messages if hasattr(exc, 'messages') else [str(exc)]
            self.message_user(
                request,
                "Activation validation failed: " + "; ".join(msgs),
                level=messages.ERROR,
            )
            return  # Abort save without raising (admin shows error message).

        super().save_model(request, obj, form, change)

        # Non-blocking warning when a second active abandoned-checkout campaign
        # exists for this store (only the first by precedence will fire).
        if obj.is_active and obj.campaign_type == "abandoned_checkout":
            if getattr(request, "store", None):
                other_exists = (
                    Campaign.objects.for_store(request.store)
                    .filter(campaign_type="abandoned_checkout", is_active=True)
                    .exclude(pk=obj.pk)
                    .exists()
                )
                if other_exists:
                    self.message_user(
                        request,
                        "Warning: another active abandoned-checkout campaign exists "
                        "for this store. Only the first (by precedence) will fire.",
                        level=messages.WARNING,
                    )

    def save_formset(self, request, form, formset, change):
        """
        Propagate the parent Campaign's store to each new CampaignStep or
        AbandonedCheckoutEmailStep.

        Both step types are StoreOwnedModel and must always have store matching
        the parent campaign's store. This override sets it on creation so the
        inline form does not need to expose the store field.

        Out-of-order delay warning: after saving AbandonedCheckoutEmailStep rows,
        emit a non-blocking WARNING when steps' send_delay_hours values are not
        strictly increasing by position (ADR-010 Q2). Does not reject the save.
        """
        instances = formset.save(commit=False)
        for instance in instances:
            if (
                isinstance(instance, (CampaignStep, AbandonedCheckoutEmailStep))
                and not instance.pk
            ):
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()

        # Non-blocking out-of-order delay warning for abandoned-checkout email steps.
        if formset.model is AbandonedCheckoutEmailStep:
            _warn_out_of_order_delays(request, self, form.instance)

    def get_urls(self):
        custom_urls = [
            path(
                '<int:pk>/activate/',
                self.admin_site.admin_view(self.activate_campaign_view),
                name='campaigns_campaign_activate',
            ),
            path(
                '<int:pk>/set-entry-step/<int:step_pk>/',
                self.admin_site.admin_view(self.set_entry_step_view),
                name='campaigns_campaign_set_entry_step',
            ),
        ]
        return custom_urls + super().get_urls()

    def render_change_form(self, request, context, add=False, change=False, form_url='', obj=None):
        """
        Inject funnel tree and activation UI context into the Campaign change form.

        T039: passes set_entry_url_fn to _build_funnel_tree_nodes so each node
        gets a 'set_entry_url' for the "Make entry" button in the template.
        """
        if obj is not None and obj.pk and obj.campaign_type != 'abandoned_checkout':
            def _step_url(pk):
                return reverse(
                    f'{self.admin_site.name}:campaigns_campaignstep_change',
                    args=[pk],
                )

            def _set_entry_url(step_pk):
                return reverse(
                    f'{self.admin_site.name}:campaigns_campaign_set_entry_step',
                    args=[obj.pk, step_pk],
                )

            context['funnel_tree'] = _build_funnel_tree_nodes(
                obj,
                step_change_url_fn=_step_url,
                set_entry_url_fn=_set_entry_url,
            )
            context['chain_preview'] = _build_chain_preview(obj)
        else:
            context['funnel_tree'] = None
            context['chain_preview'] = {"has_entry": False, "accept_chain": [], "decline_chain": []}

        # Pop activation errors stored by a previous POST to activate_campaign_view
        # (TOCTOU path: confirm POST failed re-validation).
        errors_key = f'_activation_errors_{obj.pk}' if obj and obj.pk else None
        if errors_key and errors_key in request.session:
            context['activation_errors'] = request.session.pop(errors_key)
        else:
            context['activation_errors'] = []

        # Show Activate button only for inactive, own-store, funnel-type campaigns.
        can_activate = (
            obj is not None
            and obj.pk is not None
            and not obj.is_active
            and obj.campaign_type != 'abandoned_checkout'
            and obj.owner_scope != Campaign.OWNER_SCOPE_PLATFORM
        )
        context['can_activate'] = can_activate
        if can_activate:
            context['activate_url'] = reverse(
                f'{self.admin_site.name}:campaigns_campaign_activate',
                args=[obj.pk],
            )

        return super().render_change_form(
            request, context, add=add, change=change, form_url=form_url, obj=obj
        )

    def set_entry_step_view(self, request, pk, step_pk):
        """
        POST-only action: designate a step as the campaign's entry_step (FR-B6).

        Permission-gated: rejects platform campaigns (403), uses for_store().
        CSRF-protected via the admin session authentication layer.
        On success: saves campaign.entry_step = step, redirects with success message.
        On error: returns 400 (step not in campaign) or 403 (permission denied).
        """
        from django.http import (
            HttpResponseBadRequest,
            HttpResponseForbidden,
            HttpResponseRedirect,
        )

        change_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_change',
            args=[pk],
        )

        if request.method != 'POST':
            return HttpResponseRedirect(change_url)

        store = getattr(request, 'store', None)
        if store is None:
            return HttpResponseForbidden(_("No store context for this request."))

        try:
            campaign = Campaign.objects.for_store(store).get(pk=pk)
        except Campaign.DoesNotExist:
            return HttpResponseForbidden(_("Campaign not found or access denied."))

        if not self.has_change_permission(request, campaign):
            return HttpResponseForbidden(
                _("You do not have permission to change this campaign.")
            )

        if campaign.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return HttpResponseForbidden(
                _("Platform campaigns cannot be modified from the store admin.")
            )

        try:
            step = CampaignStep.objects.for_store(store).get(pk=step_pk, campaign=campaign)
        except CampaignStep.DoesNotExist:
            return HttpResponseBadRequest(
                _("Step not found or does not belong to this campaign.")
            )

        campaign.entry_step = step
        campaign.save(update_fields=['entry_step'])
        self.message_user(
            request,
            _('Step #%(step_pk)s is now the entry step for "%(name)s".') % {
                'step_pk': step_pk,
                'name': campaign.name,
            },
            level=messages.SUCCESS,
        )
        return HttpResponseRedirect(change_url)

    def activate_campaign_view(self, request, pk):
        """
        POST-only action: validate and activate a store-scoped campaign (FR-V2).

        Two-step confirmation flow (FR-V2 — T039):
          First POST  → run validation; render activate_confirm.html showing
                        errors (blocking) / warnings (non-blocking) + funnel summary.
          Second POST → POST with confirm=1; re-validate (TOCTOU prevention) and
                        save campaign as active on success; on failure redirect to
                        change page with session errors (edge case).

        Permission-gated: rejects platform campaigns (403), uses for_store() queryset.
        """
        from django.http import HttpResponseForbidden, HttpResponseRedirect
        from django.template.response import TemplateResponse

        change_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_change',
            args=[pk],
        )

        if request.method != 'POST':
            return HttpResponseRedirect(change_url)

        store = getattr(request, 'store', None)
        if store is None:
            return HttpResponseForbidden(_("No store context for this request."))

        try:
            campaign = Campaign.objects.for_store(store).get(pk=pk)
        except Campaign.DoesNotExist:
            return HttpResponseForbidden(_("Campaign not found or access denied."))

        if not self.has_change_permission(request, campaign):
            return HttpResponseForbidden(
                _("You do not have permission to change this campaign.")
            )

        if campaign.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return HttpResponseForbidden(
                _("Platform campaigns cannot be activated from the store admin.")
            )

        # --- Validate in memory (do not save yet) ---
        campaign.is_active = True
        validation_errors = []
        try:
            from campaigns.validators import validate_campaign_activation
            validate_campaign_activation(campaign)
        except ValidationError as exc:
            validation_errors = list(exc.messages) if hasattr(exc, 'messages') else [str(exc)]
        campaign.is_active = False  # Reset; actual save happens on confirmation.

        funnel_tree = _build_funnel_tree_nodes(campaign)
        warnings = list(funnel_tree.get('warnings', []))

        confirm = request.POST.get('confirm') == '1'

        if confirm:
            # Second confirmation POST: re-validate to prevent TOCTOU, then save.
            campaign.is_active = True
            recheck_errors = []
            try:
                from campaigns.validators import validate_campaign_activation
                validate_campaign_activation(campaign)
            except ValidationError as exc:
                recheck_errors = list(exc.messages) if hasattr(exc, 'messages') else [str(exc)]

            if recheck_errors:
                campaign.is_active = False
                request.session[f'_activation_errors_{pk}'] = recheck_errors
                return HttpResponseRedirect(change_url)

            campaign.save()
            self.message_user(
                request,
                _('Campaign "%(name)s" activated successfully.') % {'name': campaign.name},
                level=messages.SUCCESS,
            )
            return HttpResponseRedirect(change_url)

        # First POST: render confirmation page.
        activate_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_activate',
            args=[pk],
        )

        summary = {}
        if not validation_errors and campaign.entry_step_id:
            all_entries = funnel_tree.get('entries', [])
            reachable_count = sum(
                1 for e in all_entries
                if not e.get('is_back_ref') and not e.get('is_cycle')
            )
            summary = {
                'entry_label': str(campaign.entry_step),
                'step_count': CampaignStep.objects.for_store(campaign.store).filter(campaign=campaign).count(),
                'reachable_count': reachable_count,
            }

        return TemplateResponse(
            request,
            'admin/campaigns/campaign/activate_confirm.html',
            {
                **self.admin_site.each_context(request),
                'campaign': campaign,
                'errors': validation_errors,
                'warnings': warnings,
                'summary': summary,
                'change_url': change_url,
                'activate_url': activate_url,
                'opts': Campaign._meta,
                'title': _('Activate Campaign'),
                'has_errors': bool(validation_errors),
            },
        )


@admin.register(CampaignStep, site=store_admin_site)
class CampaignStepAdmin(_CampaignStepAdminMixin, admin.ModelAdmin):
    """
    Per-store CampaignStep admin with translation inline.

    Provides a dedicated change view for CampaignStep so that
    CampaignStepTranslationInline can be attached. Steps are still
    accessible from the Campaign inline, but translations require
    navigating to the step's own change page.

    T039 additions:
    - Uses CampaignStepAdminForm (FR-S1/S2/S3): structured fields replace raw
      offer_config_json editing.
    - accept_next_step / decline_next_step use Select widgets scoped to the
      current campaign (FR-B5): raw_id_fields removed for those FKs.
    - delete_view: warns about steps whose branch FKs will be nulled (FR-B8,
      via _CampaignStepAdminMixin).

    module_key: ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6) — same CAMPAIGNS_MODULE_KEY as
    CampaignAdmin. has_add/change/delete_permission are overridden below
    rather than left to the generic mapping (see CampaignAdmin's docstring
    for why: their previous super() calls reached Django's stock,
    Permission-row-based check, which is always False for real employees).
    """

    module_key = CAMPAIGNS_MODULE_KEY

    form = CampaignStepAdminForm
    list_display = ["campaign", "position", "offer_type", "translation_status_html", "store"]
    list_filter = ["offer_type"]
    search_fields = ["campaign__name"]
    # campaign still uses raw_id to avoid loading all campaigns into memory.
    # accept_next_step / decline_next_step are NOT in raw_id_fields: they use
    # campaign-scoped Select widgets instead (FR-B5 — T039).
    raw_id_fields = ["campaign"]
    inlines = [CampaignStepTranslationInline]
    exclude = ["store"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return (
                CampaignStep.objects.for_store(request.store)
                .select_related("campaign")
            )
        return CampaignStep.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        FR-B5 (T039): scope accept_next_step / decline_next_step dropdowns to
        the campaign being edited, excluding the step itself.

        Replaces raw_id_fields behaviour for branch FKs so the admin cannot
        wire a step to a step from another campaign (which validate_campaign_
        activation would later reject at activation time, but silently saving
        cross-campaign FKs is a UX hazard).

        campaign FK: still scoped to the store so raw_id validation passes.
        """
        if db_field.name in ("accept_next_step", "decline_next_step"):
            campaign_id = self._get_campaign_id_from_request(request)
            if campaign_id:
                qs = CampaignStep.objects.cross_store_unsafe().filter(
                    campaign_id=campaign_id
                )
                current_step_id = self._get_current_step_id(request)
                if current_step_id:
                    try:
                        qs = qs.exclude(pk=int(current_step_id))
                    except (ValueError, TypeError):
                        pass
                kwargs["queryset"] = qs

            # Descriptive empty-choice labels clarify terminal semantics (FR-B5).
            if db_field.name == "accept_next_step":
                kwargs["empty_label"] = _("— end funnel (terminal: CONVERTED)")
            else:
                kwargs["empty_label"] = _("— end funnel (terminal: DISMISSED)")

        if db_field.name == "campaign":
            # campaign targets Campaign (StoreOwnedModel); use the safe raw-id
            # widget so label_and_url_for_value() doesn't crash via the
            # raising default manager (RAW-ID-WIDGET-ISOLATION,
            # core/admin_widgets.py). Queryset scoping below is unchanged.
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
            if getattr(request, "store", None):
                kwargs["queryset"] = Campaign.objects.for_store(request.store)

        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def has_change_permission(self, request, obj=None):
        """
        Prevent mutating a step that belongs to a platform campaign (ADR-009 §2).

        Without this check, a store-admin can navigate directly to
        /admin/campaigns/campaignstep/<id>/change/ for a platform-owned step
        and edit it, bypassing the read-only enforcement on the Campaign form.
        """
        if obj is not None and obj.campaign.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        return check_module_access(request, self.module_key, "full")

    def has_delete_permission(self, request, obj=None):
        """
        Prevent deleting a step that belongs to a platform campaign (ADR-009 §2).

        Also blocks deletion when live (non-terminal) sessions have current_step
        pointing at this step (M3 secondary guard). Deleting a step that live
        sessions are on would leave those sessions in an irrecoverable state and
        break the accept/decline flow for active buyers.

        Uses for_store(obj.store) to stay within the tenant boundary.
        """
        if obj is not None and obj.campaign.owner_scope == Campaign.OWNER_SCOPE_PLATFORM:
            return False
        if obj is not None and obj.store_id is not None:
            live_count = (
                CampaignSession.objects
                .for_store(obj.store)
                .filter(
                    current_step=obj,
                    state__in=NON_TERMINAL_STATES,
                )
                .count()
            )
            if live_count > 0:
                return False
        return check_module_access(request, self.module_key, "full")

    def change_view(self, request, object_id, form_url='', extra_context=None):
        """
        Emit a warning banner when the campaign is active so the admin knows
        that price/offer fields are locked (M3 secondary guard).

        The warning appears on both GET (page load) and POST (failed validation
        re-render) so it is always visible while the session is in the active
        state. The readonly enforcement itself lives in get_readonly_fields.
        """
        obj = self.get_object(request, object_id)
        if obj is not None and obj.campaign.is_active:
            messages.warning(
                request,
                _(
                    "This campaign is active. Price and offer fields are locked "
                    "to prevent charging customers a different amount than shown."
                ),
            )
        return super().change_view(request, object_id, form_url, extra_context)

    def save_model(self, request, obj, form, change):
        """Set store on new CampaignStep instances."""
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def save_formset(self, request, form, formset, change):
        """Propagate step's store to each new CampaignStepTranslation."""
        instances = formset.save(commit=False)
        for instance in instances:
            if isinstance(instance, CampaignStepTranslation) and not instance.pk:
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()


@admin.register(CampaignSession, site=store_admin_site)
class CampaignSessionAdmin(admin.ModelAdmin):
    """
    View-only CampaignSession admin for the store surface.

    All state machine transitions are code-driven (service layer + webhooks).
    Admin staff can view session history but cannot create or edit rows.
    """

    module_key = CAMPAIGNS_MODULE_KEY

    list_display = ["pk", "order", "cart", "campaign", "campaign_type", "state", "converted_at"]
    list_filter = ["state", "campaign_type"]
    ordering = ["-created_at"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return CampaignSession.objects.for_store(request.store)
        return CampaignSession.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(OrderCharge, site=store_admin_site)
class OrderChargeAdmin(admin.ModelAdmin):
    """
    View-only OrderCharge admin for the store surface.

    Charges are created by the payment webhook handler (T028), never via admin.

    §6.4 (T039): charge_type, status, and campaign_step_id added to list_display
    and list_filter so store admins can distinguish ORIGINAL vs UPSELL charges
    and filter by lifecycle status.
    """

    module_key = CAMPAIGNS_MODULE_KEY

    list_display = [
        "pk",
        "order",
        "charge_type",
        "status",
        "amount_cents",
        "processor_charge_id",
        "campaign_step_id",
        "captured_at",
        "created_at",
    ]
    list_filter = ["charge_type", "status"]
    search_fields = ["processor_charge_id", "processor_payment_intent_id", "order__order_number"]
    readonly_fields = ["created_at"]
    ordering = ["-created_at"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return OrderCharge.objects.for_store(request.store)
        return OrderCharge.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(CampaignIssuedCode, site=store_admin_site)
class CampaignIssuedCodeAdmin(admin.ModelAdmin):
    """
    View-only CampaignIssuedCode admin for the store surface (audit table).

    Issued codes are created exclusively by the T021 send task or T028 accept-
    transaction hook. No add, change, or delete is permitted on any admin surface
    — this table is an attribution/idempotency audit log (ADR-009 §3).
    """

    module_key = CAMPAIGNS_MODULE_KEY

    list_display = ["campaign_session", "step", "email_step", "discount_code", "issued_at"]
    ordering = ["-issued_at"]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return CampaignIssuedCode.objects.for_store(request.store)
        return CampaignIssuedCode.objects.none()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# Super admin
# ---------------------------------------------------------------------------

@admin.register(Campaign, site=super_admin_site)
class CampaignSuperAdmin(admin.ModelAdmin):
    """
    Cross-store Campaign admin for the super-admin surface.

    owner_scope defaults to 'platform' for new objects created from the
    super-admin surface. Full CRUD on both scopes.

    Inlines: same type-gated logic as CampaignAdmin.

    T039 additions: same set_entry_step_view and two-step activate_campaign_view
    as CampaignAdmin, but cross-store (no store-scoping on queryset).
    """

    list_display = ["name", "campaign_type", "owner_scope", "is_active", "store"]
    list_filter = ["campaign_type", "owner_scope", "is_active"]
    search_fields = ["name"]
    raw_id_fields = ["entry_step"]

    def get_inline_instances(self, request, obj=None):
        """Show only the inline relevant to campaign_type."""
        campaign_type = None
        if obj is not None:
            campaign_type = obj.campaign_type
        elif request.method == "POST":
            campaign_type = request.POST.get("campaign_type")

        instances = []
        if campaign_type == "abandoned_checkout":
            instances.append(AbandonedCheckoutEmailStepInline(self.model, self.admin_site))
        elif campaign_type is not None:
            instances.append(CampaignStepInline(self.model, self.admin_site))
        return instances

    def get_queryset(self, request):
        return Campaign.objects.cross_store_unsafe()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        entry_step targets CampaignStep (StoreOwnedModel); use the safe
        raw-id widget so label_and_url_for_value() doesn't crash via the
        raising default manager the instant this field re-renders with a
        bound value (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py). Does
        not add or change queryset scoping for the field.
        """
        if db_field.name == "entry_step":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def get_changeform_initial_data(self, request):
        """Default owner_scope to 'platform' for campaigns created from super-admin."""
        initial = super().get_changeform_initial_data(request)
        initial.setdefault("owner_scope", Campaign.OWNER_SCOPE_PLATFORM)
        return initial

    def save_model(self, request, obj, form, change):
        """
        Run activation gate validation before saving.
        """
        try:
            from campaigns.validators import validate_campaign_activation
            validate_campaign_activation(obj)
        except ValidationError as exc:
            msgs = exc.messages if hasattr(exc, 'messages') else [str(exc)]
            self.message_user(
                request,
                "Activation validation failed: " + "; ".join(msgs),
                level=messages.ERROR,
            )
            return

        super().save_model(request, obj, form, change)

    def save_formset(self, request, form, formset, change):
        """
        Propagate the parent Campaign's store to each new CampaignStep or
        AbandonedCheckoutEmailStep.

        Out-of-order delay warning: after saving AbandonedCheckoutEmailStep rows,
        emit a non-blocking WARNING when steps' send_delay_hours values are not
        strictly increasing by position (ADR-010 Q2). Does not reject the save.
        """
        instances = formset.save(commit=False)
        for instance in instances:
            if (
                isinstance(instance, (CampaignStep, AbandonedCheckoutEmailStep))
                and not instance.pk
            ):
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()

        # Non-blocking out-of-order delay warning for abandoned-checkout email steps.
        if formset.model is AbandonedCheckoutEmailStep:
            _warn_out_of_order_delays(request, self, form.instance)

    def get_urls(self):
        custom_urls = [
            path(
                '<int:pk>/activate/',
                self.admin_site.admin_view(self.activate_campaign_view),
                name='campaigns_campaign_activate',
            ),
            path(
                '<int:pk>/set-entry-step/<int:step_pk>/',
                self.admin_site.admin_view(self.set_entry_step_view),
                name='campaigns_campaign_set_entry_step',
            ),
        ]
        return custom_urls + super().get_urls()

    def render_change_form(self, request, context, add=False, change=False, form_url='', obj=None):
        """
        Inject funnel tree and activation UI context into the Campaign change form
        (super-admin). Passes set_entry_url_fn for "Make entry" buttons (FR-B6).
        """
        if obj is not None and obj.pk and obj.campaign_type != 'abandoned_checkout':
            def _step_url(pk):
                return reverse(
                    f'{self.admin_site.name}:campaigns_campaignstep_change',
                    args=[pk],
                )

            def _set_entry_url(step_pk):
                return reverse(
                    f'{self.admin_site.name}:campaigns_campaign_set_entry_step',
                    args=[obj.pk, step_pk],
                )

            context['funnel_tree'] = _build_funnel_tree_nodes(
                obj,
                step_change_url_fn=_step_url,
                set_entry_url_fn=_set_entry_url,
            )
            context['chain_preview'] = _build_chain_preview(obj)
        else:
            context['funnel_tree'] = None
            context['chain_preview'] = {"has_entry": False, "accept_chain": [], "decline_chain": []}

        errors_key = f'_activation_errors_{obj.pk}' if obj and obj.pk else None
        if errors_key and errors_key in request.session:
            context['activation_errors'] = request.session.pop(errors_key)
        else:
            context['activation_errors'] = []

        # Super-admin can activate any campaign (no owner_scope restriction).
        can_activate = (
            obj is not None
            and obj.pk is not None
            and not obj.is_active
            and obj.campaign_type != 'abandoned_checkout'
        )
        context['can_activate'] = can_activate
        if can_activate:
            context['activate_url'] = reverse(
                f'{self.admin_site.name}:campaigns_campaign_activate',
                args=[obj.pk],
            )

        return super().render_change_form(
            request, context, add=add, change=change, form_url=form_url, obj=obj
        )

    def set_entry_step_view(self, request, pk, step_pk):
        """
        POST-only action: designate a step as the campaign's entry_step (FR-B6).
        Cross-store (super-admin surface) — no store-scoping.
        """
        from django.http import (
            HttpResponseBadRequest,
            HttpResponseForbidden,
            HttpResponseRedirect,
        )

        change_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_change',
            args=[pk],
        )

        if request.method != 'POST':
            return HttpResponseRedirect(change_url)

        try:
            campaign = Campaign.objects.cross_store_unsafe().get(pk=pk)
        except Campaign.DoesNotExist:
            return HttpResponseForbidden(_("Campaign not found."))

        if not self.has_change_permission(request, campaign):
            return HttpResponseForbidden(
                _("You do not have permission to change this campaign.")
            )

        try:
            step = CampaignStep.objects.cross_store_unsafe().get(
                pk=step_pk, campaign=campaign
            )
        except CampaignStep.DoesNotExist:
            return HttpResponseBadRequest(
                _("Step not found or does not belong to this campaign.")
            )

        campaign.entry_step = step
        campaign.save(update_fields=['entry_step'])
        self.message_user(
            request,
            _('Step #%(step_pk)s is now the entry step for "%(name)s".') % {
                'step_pk': step_pk,
                'name': campaign.name,
            },
            level=messages.SUCCESS,
        )
        return HttpResponseRedirect(change_url)

    def activate_campaign_view(self, request, pk):
        """
        POST-only action: validate and activate any campaign (cross-store, super-admin).
        Two-step confirmation flow (FR-V2 — T039).
        """
        from django.http import HttpResponseForbidden, HttpResponseRedirect
        from django.template.response import TemplateResponse

        change_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_change',
            args=[pk],
        )

        if request.method != 'POST':
            return HttpResponseRedirect(change_url)

        try:
            campaign = Campaign.objects.cross_store_unsafe().get(pk=pk)
        except Campaign.DoesNotExist:
            return HttpResponseForbidden(_("Campaign not found."))

        if not self.has_change_permission(request, campaign):
            return HttpResponseForbidden(
                _("You do not have permission to change this campaign.")
            )

        # Validate in memory.
        campaign.is_active = True
        validation_errors = []
        try:
            from campaigns.validators import validate_campaign_activation
            validate_campaign_activation(campaign)
        except ValidationError as exc:
            validation_errors = list(exc.messages) if hasattr(exc, 'messages') else [str(exc)]
        campaign.is_active = False

        funnel_tree = _build_funnel_tree_nodes(campaign)
        warnings = list(funnel_tree.get('warnings', []))

        confirm = request.POST.get('confirm') == '1'

        if confirm:
            campaign.is_active = True
            recheck_errors = []
            try:
                from campaigns.validators import validate_campaign_activation
                validate_campaign_activation(campaign)
            except ValidationError as exc:
                recheck_errors = list(exc.messages) if hasattr(exc, 'messages') else [str(exc)]

            if recheck_errors:
                campaign.is_active = False
                request.session[f'_activation_errors_{pk}'] = recheck_errors
                return HttpResponseRedirect(change_url)

            campaign.save()
            self.message_user(
                request,
                _('Campaign "%(name)s" activated successfully.') % {'name': campaign.name},
                level=messages.SUCCESS,
            )
            return HttpResponseRedirect(change_url)

        # First POST: render confirmation page.
        activate_url = reverse(
            f'{self.admin_site.name}:campaigns_campaign_activate',
            args=[pk],
        )

        summary = {}
        if not validation_errors and campaign.entry_step_id:
            all_entries = funnel_tree.get('entries', [])
            reachable_count = sum(
                1 for e in all_entries
                if not e.get('is_back_ref') and not e.get('is_cycle')
            )
            summary = {
                'entry_label': str(campaign.entry_step),
                'step_count': CampaignStep.objects.for_store(campaign.store).filter(campaign=campaign).count(),
                'reachable_count': reachable_count,
            }

        return TemplateResponse(
            request,
            'admin/campaigns/campaign/activate_confirm.html',
            {
                **self.admin_site.each_context(request),
                'campaign': campaign,
                'errors': validation_errors,
                'warnings': warnings,
                'summary': summary,
                'change_url': change_url,
                'activate_url': activate_url,
                'opts': Campaign._meta,
                'title': _('Activate Campaign'),
                'has_errors': bool(validation_errors),
            },
        )


@admin.register(CampaignStep, site=super_admin_site)
class CampaignStepSuperAdmin(_CampaignStepAdminMixin, admin.ModelAdmin):
    """
    Cross-store CampaignStep admin for the super-admin surface, with translation
    inline and structured form (FR-S1/S2/S3).

    offer_config_json is shown as readonly (raw JSON view for debugging) since
    CampaignStepAdminForm excludes it from the editable form; super admins can
    still read the raw config while structured fields handle editing (FR-S1).
    """

    form = CampaignStepAdminForm
    list_display = ["campaign", "position", "offer_type", "translation_status_html", "store"]
    list_filter = ["offer_type"]
    search_fields = ["campaign__name"]
    raw_id_fields = ["campaign"]
    readonly_fields = ["offer_config_json"]
    inlines = [CampaignStepTranslationInline]
    exclude = ["store"]

    def get_queryset(self, request):
        return CampaignStep.objects.cross_store_unsafe()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        FR-B5 (T039): scope accept_next_step / decline_next_step dropdowns to the
        campaign being edited, excluding the step itself (cross-store).

        campaign FK: uses the safe raw-id widget (RAW-ID-WIDGET-ISOLATION,
        core/admin_widgets.py) since it targets Campaign (StoreOwnedModel) and
        would otherwise 500 in label_and_url_for_value() the instant this
        field re-renders with a bound value. Does not add or change queryset
        scoping for the field.
        """
        if db_field.name in ("accept_next_step", "decline_next_step"):
            campaign_id = self._get_campaign_id_from_request(request)
            if campaign_id:
                qs = CampaignStep.objects.cross_store_unsafe().filter(
                    campaign_id=campaign_id
                )
                current_step_id = self._get_current_step_id(request)
                if current_step_id:
                    try:
                        qs = qs.exclude(pk=int(current_step_id))
                    except (ValueError, TypeError):
                        pass
                kwargs["queryset"] = qs

            if db_field.name == "accept_next_step":
                kwargs["empty_label"] = _("— end funnel (terminal: CONVERTED)")
            else:
                kwargs["empty_label"] = _("— end funnel (terminal: DISMISSED)")

        if db_field.name == "campaign":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )

        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        # store is set from campaign when creating a new step; for existing steps
        # it is already set.
        if not change and not obj.store_id and obj.campaign_id:
            obj.store = obj.campaign.store
        super().save_model(request, obj, form, change)

    def save_formset(self, request, form, formset, change):
        """Propagate step's store to each new CampaignStepTranslation."""
        instances = formset.save(commit=False)
        for instance in instances:
            if isinstance(instance, CampaignStepTranslation) and not instance.pk:
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()


@admin.register(CampaignSession, site=super_admin_site)
class CampaignSessionSuperAdmin(admin.ModelAdmin):
    """
    View-only CampaignSession admin for the super-admin surface.
    """

    list_display = ["pk", "order", "cart", "campaign", "campaign_type", "state", "converted_at"]
    list_filter = ["state", "campaign_type"]
    ordering = ["-created_at"]

    def get_queryset(self, request):
        return CampaignSession.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(OrderCharge, site=super_admin_site)
class OrderChargeSuperAdmin(admin.ModelAdmin):
    """
    View-only OrderCharge admin for the super-admin surface.

    §6.4 (T039): charge_type, status, and campaign_step_id added to list_display
    and list_filter so super-admins can audit ORIGINAL vs UPSELL charges across
    all stores and filter by type or status.
    """

    list_display = [
        "pk",
        "order",
        "charge_type",
        "status",
        "amount_cents",
        "processor_charge_id",
        "campaign_step_id",
        "captured_at",
        "created_at",
    ]
    list_filter = ["charge_type", "status", "store"]
    search_fields = ["processor_charge_id", "processor_payment_intent_id", "order__order_number"]
    readonly_fields = ["created_at"]
    ordering = ["-created_at"]

    def get_queryset(self, request):
        return OrderCharge.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(CampaignSessionToken, site=super_admin_site)
class CampaignSessionTokenSuperAdmin(admin.ModelAdmin):
    """
    View-only CampaignSessionToken admin — super-admin only.

    Tokens are generated by the service layer; raw values are never stored.
    Super-admins can inspect token metadata (purpose, expiry, used_at) for
    debugging and audit purposes, but cannot create or modify tokens.
    """

    list_display = ["campaign_session", "purpose", "expires_at", "used_at"]
    list_filter = ["purpose"]
    ordering = ["-expires_at"]

    def get_queryset(self, request):
        return CampaignSessionToken.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(CampaignIssuedCode, site=super_admin_site)
class CampaignIssuedCodeSuperAdmin(admin.ModelAdmin):
    """
    View-only CampaignIssuedCode admin for the super-admin surface (audit table).

    No add, change, or delete permitted — attribution/idempotency audit log only.
    """

    list_display = ["campaign_session", "step", "email_step", "discount_code", "issued_at", "store"]
    ordering = ["-issued_at"]

    def get_queryset(self, request):
        return CampaignIssuedCode.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False
