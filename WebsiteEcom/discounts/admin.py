"""
Discount admin registrations.

Two admin classes per model:
- Store-scoped class → store_admin_site  (merchant sees only their store's records)
- Cross-store class  → super_admin_site  (Pradize operators see all records)

CampaignReward is read-only on both sites — records are created programmatically
by the campaign reward service, never manually. The admin is an audit view only.

DiscountCodeAdmin fieldsets:
  1. Code basics     — code, discount_type, value_type, value
  2. Gift card       — initial_balance
  3. Limits          — minimum_order_amount, starts_at, ends_at, usage_limit,
                       usage_limit_per_customer, per_email_limit
  4. Conditions      — product_conditions, collection_conditions
  5. Audit           — provenance, stackable, currency, free_product
  6. Campaign        — gift_card_campaign (read-only, D8 audit link), refund_flagged,
                       refund_flagged_reason (ADR-029 D9)
  7. Status          — is_active, times_used (read-only), created_at, updated_at

GiftCardCampaign (ADR-029, TICKET-045) is super-admin only in v1 (D10: store
admins see nothing — owner_scope keeps the store-admin variant open for later).
Registered only on super_admin_site, cross-store via .cross_store_unsafe() —
the same convention as campaigns.admin.CampaignSuperAdmin.
"""

from django import forms
from django.contrib import admin
from django.urls import reverse
from django.utils.html import format_html

from core.admin import StoreOwnedInlineMixin
from core.widgets import DisabledOptionsSelect
from webecom.admin import store_admin_site, super_admin_site

from .models import (
    CampaignReward,
    DiscountCode,
    DiscountCodeEmailUse,
    GiftCardCampaign,
    GiftCardCampaignStatus,
    GiftCardCampaignTriggerScope,
    GiftCardCampaignValueMode,
    GiftCardTransaction,
)


# ---------------------------------------------------------------------------
# Inlines
# ---------------------------------------------------------------------------


class GiftCardTransactionInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Read-only inline shown on BOTH DiscountCodeAdmin (store) and
    DiscountCodeSuperAdmin (super) change forms.

    StoreOwnedInlineMixin supplies get_queryset() -> cross_store_unsafe():
    safe because Django's InlineModelAdmin filters the formset queryset by
    the parent DiscountCode's FK after get_queryset() runs — the rows are
    already scoped to one specific parent instance, and that parent was
    itself reached through the calling admin's own store-scoped/cross-store
    get_queryset(). It also supplies the StoreSafeInlineFormSet needed to
    POST an existing row (INLINE-FORMSET-PK-ISOLATION, core/formsets.py).
    """

    model = GiftCardTransaction
    extra = 0
    readonly_fields = ("amount", "order", "note", "created_at")
    can_delete = False
    show_change_link = False
    fields = ("amount", "order", "note", "created_at")

    def has_add_permission(self, request, obj=None):
        return False


class CampaignRewardInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Read-only inline shown on BOTH DiscountCodeAdmin (store) and
    DiscountCodeSuperAdmin (super) change forms.

    See GiftCardTransactionInline docstring above for why
    StoreOwnedInlineMixin's cross_store_unsafe() convention is safe here
    (parent-FK scoping already applies).
    """

    model = CampaignReward
    extra = 0
    readonly_fields = ("campaign_id", "recipient_email", "issued_at")
    can_delete = False
    show_change_link = True
    fields = ("campaign_id", "recipient_email", "issued_at")

    def has_add_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# D10 — issued-cards audit "Campaign" filter (ADR-029)
# ---------------------------------------------------------------------------


class GiftCardCampaignListFilter(admin.SimpleListFilter):
    """
    Filters the DiscountCode changelist by gift_card_campaign (D10: "the
    existing admin-020 issued-gift-cards list gains a 'Campaign' filter").

    A plain FK entry in list_filter would build its dropdown via
    GiftCardCampaign._default_manager.get_queryset() — the RaisingQuerySet
    every StoreOwnedModel manager returns for unscoped access (core/managers.py)
    — and crash the changelist. This SimpleListFilter instead builds its
    choices through .for_store()/.cross_store_unsafe() explicitly, the same
    pattern raw_id_fields sidesteps for FK widgets elsewhere in this codebase
    (e.g. campaigns/admin.py's `raw_id_fields = ["campaign"]`).
    """

    title = "gift card campaign"
    parameter_name = "gift_card_campaign"

    def lookups(self, request, model_admin):
        store = getattr(request, "store", None)
        if store is not None:
            campaigns = GiftCardCampaign.objects.for_store(store)
        else:
            campaigns = GiftCardCampaign.objects.cross_store_unsafe()
        return [(c.pk, c.name) for c in campaigns.order_by("name")]

    def queryset(self, request, queryset):
        value = self.value()
        if value:
            return queryset.filter(gift_card_campaign_id=value)
        return queryset


# ---------------------------------------------------------------------------
# DiscountCode — store admin
# ---------------------------------------------------------------------------


@admin.register(DiscountCode, site=store_admin_site)
class DiscountCodeAdmin(admin.ModelAdmin):
    """
    Store-scoped discount code admin.

    times_used is read-only — always updated atomically by DiscountService.
    free_product is limited to the store's own products via get_form() so that
    store admins cannot reference another tenant's product.

    module_key="gift_cards" (ADR-033 D3c/D7 item 4 — DECIDED, human
    2026-07-11: ADR-002 unified coupons+gift cards into DiscountCode; AF-002
    has a "Gift cards" row but no separate "Coupons" row).
    """

    module_key = "gift_cards"

    list_display = (
        "code",
        "discount_type",
        "value_type",
        "value",
        "provenance",
        "stackable",
        "currency",
        "is_active",
        "times_used",
        "ends_at",
        "gift_card_campaign",
        "refund_flagged",
    )
    list_filter = (
        "discount_type",
        "value_type",
        "provenance",
        "is_active",
        "stackable",
        GiftCardCampaignListFilter,
        "refund_flagged",
    )
    search_fields = ("code",)
    readonly_fields = (
        "times_used",
        "created_at",
        "updated_at",
        "gift_card_campaign",
    )
    inlines = [GiftCardTransactionInline, CampaignRewardInline]
    fieldsets = (
        (
            "Code basics",
            {
                "fields": ("code", "discount_type", "value_type", "value"),
            },
        ),
        (
            "Gift card",
            {
                "fields": ("initial_balance",),
                "classes": ("collapse",),
                "description": "Only relevant for gift_card_manual / gift_card_auto types.",
            },
        ),
        (
            "Limits",
            {
                "fields": (
                    "minimum_order_amount",
                    "starts_at",
                    "ends_at",
                    "usage_limit",
                    "usage_limit_per_customer",
                    "per_email_limit",
                ),
            },
        ),
        (
            "Conditions",
            {
                "fields": ("product_conditions", "collection_conditions"),
                "description": "Leave both empty to apply to all products.",
            },
        ),
        (
            "Audit & stacking",
            {
                "fields": ("provenance", "stackable", "currency", "free_product"),
                "description": (
                    "Provenance tracks how this code was created. "
                    "Stackable controls whether the customer can combine it with a gift card. "
                    "Currency restricts to a specific ISO 4217 currency (empty = all). "
                    "Free product is required when value_type is 'Free product'."
                ),
            },
        ),
        (
            "Automated gift-card campaign (ADR-029)",
            {
                "fields": ("gift_card_campaign", "refund_flagged", "refund_flagged_reason"),
                "classes": ("collapse",),
                "description": (
                    "gift_card_campaign is set programmatically at issuance — never "
                    "manually assigned. refund_flagged is set when a spent card's "
                    "triggering order is refunded (D9); the balance is never clawed "
                    "back automatically — review manually and deactivate if warranted."
                ),
            },
        ),
        (
            "Status",
            {
                "fields": ("is_active", "times_used", "created_at", "updated_at"),
            },
        ),
    )

    def get_queryset(self, request):
        """Scope to the requesting user's store. Returns empty queryset when store is absent."""
        store = getattr(request, "store", None)
        if store is None:
            return DiscountCode.objects.none()
        return DiscountCode.objects.for_store(store)

    def get_form(self, request, obj=None, **kwargs):
        """Restrict free_product FK choices to the current store's products."""
        form = super().get_form(request, obj, **kwargs)
        if hasattr(request, "store") and "free_product" in form.base_fields:
            from catalog.models import Product

            form.base_fields["free_product"].queryset = Product.objects.for_store(
                request.store
            )
        return form

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        """
        Scope product_conditions/collection_conditions to the current store
        (ADR-031 TICKET-050): Django's ManyToManyField.formfield() otherwise
        defaults `queryset` to `<target>._default_manager.using(...)` —
        catalog.Product/Collection's own raising StoreScopedManager
        (core/managers.py). That default choice list happens to render fine
        (ModelChoiceIterator uses QuerySet.iterator(), which _RaisingQuerySet
        does not intercept) but crashes with IsolationError the moment a
        selection is POSTed and validated (ModelMultipleChoiceField.clean()
        iterates the queryset the normal way). Scoping here fixes both the
        crash and the pre-fix hazard of offering another store's rows at all.
        """
        store = getattr(request, "store", None)
        if store is not None:
            if db_field.name == "product_conditions":
                from catalog.models import Product

                kwargs["queryset"] = Product.objects.for_store(store)
            elif db_field.name == "collection_conditions":
                from catalog.models import Collection

                kwargs["queryset"] = Collection.objects.for_store(store)
        return super().formfield_for_manytomany(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        """Auto-assign the store on creation."""
        if not change and hasattr(request, "store"):
            obj.store = request.store
        super().save_model(request, obj, form, change)


# ---------------------------------------------------------------------------
# DiscountCode — super admin
# ---------------------------------------------------------------------------


@admin.register(DiscountCode, site=super_admin_site)
class DiscountCodeSuperAdmin(admin.ModelAdmin):
    """
    Cross-store discount code view for Pradize operators.

    get_queryset (pre-existing gap, fixed in passing while adding the ADR-029
    'Campaign' filter below): DiscountCode is a StoreOwnedModel whose default
    manager raises IsolationError on unscoped access (core/managers.py) — the
    plain admin.ModelAdmin.get_queryset() this class relied on before is
    exactly that unscoped access, and would crash the changelist page for any
    operator who opened it. Fixed using the same .cross_store_unsafe()
    convention already used by every other cross-store admin in this codebase
    (e.g. campaigns.admin.CampaignSuperAdmin, orders.admin's Order super admin).
    """

    list_display = (
        "code",
        "store",
        "discount_type",
        "value_type",
        "value",
        "provenance",
        "stackable",
        "currency",
        "is_active",
        "times_used",
        "ends_at",
        "gift_card_campaign",
        "refund_flagged",
    )
    list_filter = (
        "store",
        "discount_type",
        "value_type",
        "provenance",
        "is_active",
        "stackable",
        GiftCardCampaignListFilter,
        "refund_flagged",
    )
    search_fields = ("code", "store__name")
    readonly_fields = ("times_used", "created_at", "updated_at", "gift_card_campaign")
    inlines = [GiftCardTransactionInline, CampaignRewardInline]
    fieldsets = (
        (
            "Code basics",
            {
                "fields": ("store", "code", "discount_type", "value_type", "value"),
            },
        ),
        (
            "Gift card",
            {
                "fields": ("initial_balance",),
                "classes": ("collapse",),
            },
        ),
        (
            "Limits",
            {
                "fields": (
                    "minimum_order_amount",
                    "starts_at",
                    "ends_at",
                    "usage_limit",
                    "usage_limit_per_customer",
                    "per_email_limit",
                ),
            },
        ),
        (
            "Conditions",
            {
                "fields": ("product_conditions", "collection_conditions"),
            },
        ),
        (
            "Audit & stacking",
            {
                "fields": ("provenance", "stackable", "currency", "free_product"),
            },
        ),
        (
            "Automated gift-card campaign (ADR-029)",
            {
                "fields": ("gift_card_campaign", "refund_flagged", "refund_flagged_reason"),
                "classes": ("collapse",),
            },
        ),
        (
            "Status",
            {
                "fields": ("is_active", "times_used", "created_at", "updated_at"),
            },
        ),
    )

    def get_queryset(self, request):
        return DiscountCode.objects.cross_store_unsafe()

    def get_form(self, request, obj=None, **kwargs):
        """
        Cross-store equivalent of DiscountCodeAdmin.get_form's free_product
        scoping above — found missing while auditing terminal-method
        isolation fallout for TICKET-051 (ADR-031 addendum): without this
        override, ModelForm's auto-generated free_product field defaults to
        `Product._default_manager` (the raising StoreScopedManager), and
        iterating its choices to render the widget crashed with
        IsolationError on this admin's own add/change view. Scoped to the
        edited code's own store on change (mirrors formfield_for_manytomany's
        object_id pattern below); cross_store_unsafe() on the add form, where
        there is no instance yet to scope by.
        """
        form = super().get_form(request, obj, **kwargs)
        if "free_product" in form.base_fields:
            from catalog.models import Product

            if obj is not None:
                form.base_fields["free_product"].queryset = Product.objects.for_store(obj.store)
            else:
                form.base_fields["free_product"].queryset = Product.objects.cross_store_unsafe()
        return form

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        """
        Cross-store equivalent of DiscountCodeAdmin.formfield_for_manytomany
        above (ADR-031 TICKET-050) — same crash, same fix, but scoped to the
        edited DiscountCode's own store on change (mirrors
        GiftCardCampaignAdmin.formfield_for_manytomany's object_id pattern);
        on the add form there is no instance yet, so cross_store_unsafe() is
        used (a super-admin creating a code has no store context to scope by)
        and the write-side core/m2m_guard.py guard is the safety net.
        """
        if db_field.name in ("product_conditions", "collection_conditions"):
            from catalog.models import Collection, Product

            target_model = Product if db_field.name == "product_conditions" else Collection
            obj_id = request.resolver_match.kwargs.get("object_id") if request.resolver_match else None
            queryset = target_model.objects.cross_store_unsafe()
            if obj_id:
                try:
                    code = DiscountCode.objects.cross_store_unsafe().get(pk=obj_id)
                    queryset = target_model.objects.for_store(code.store)
                except (DiscountCode.DoesNotExist, ValueError, TypeError):
                    pass
            kwargs["queryset"] = queryset
        return super().formfield_for_manytomany(db_field, request, **kwargs)


# ---------------------------------------------------------------------------
# CampaignReward — both sites (read-only audit view)
# ---------------------------------------------------------------------------


class CampaignRewardAdminBase(admin.ModelAdmin):
    """
    Read-only audit view for campaign-issued rewards.

    Records are written by the campaign reward service, never manually.
    All fields are read-only. Add and delete are disabled.
    """

    list_display = ("campaign_id", "recipient_email", "discount_code", "store", "issued_at")
    list_filter = ("store",)
    search_fields = ("campaign_id", "recipient_email", "discount_code__code")
    readonly_fields = ("store", "campaign_id", "recipient_email", "discount_code", "issued_at")
    ordering = ("-issued_at",)

    def has_add_permission(self, request):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False


@admin.register(CampaignReward, site=store_admin_site)
class CampaignRewardStoreAdmin(CampaignRewardAdminBase):
    """Store-scoped campaign reward audit view."""

    module_key = "gift_cards"

    def get_queryset(self, request):
        store = getattr(request, "store", None)
        if store is None:
            return CampaignReward.objects.none()
        return CampaignReward.objects.for_store(store)


@admin.register(CampaignReward, site=super_admin_site)
class CampaignRewardSuperAdmin(CampaignRewardAdminBase):
    """Cross-store campaign reward audit view for Pradize operators."""

    list_filter = ("store",)


# ---------------------------------------------------------------------------
# GiftCardCampaign — super admin only (ADR-029 D10, TICKET-045)
#
# Store admins see nothing in v1 (owner_scope keeps the store-admin variant
# open for later without a migration) — no registration on store_admin_site.
# ---------------------------------------------------------------------------


class GiftCardCampaignAdminForm(forms.ModelForm):
    """
    Adds the one check Model.clean() cannot reliably perform for an unsaved
    instance: Django's ModelForm excludes ManyToMany fields from
    instance.full_clean() during the normal add/edit flow (they are synced via
    save_m2m() after the instance is saved), so
    discounts.validators.validate_gift_card_campaign()'s trigger_products
    check only fires for an already-saved instance (defense in depth). This
    form checks the submitted (not-yet-saved) trigger_products selection
    directly from cleaned_data instead.

    Reserved/PENDING choices (ADR-029 D2 trigger_scope='conditions', D3
    value_mode='percent_of_order'/'percent_discount_coupon') are rendered
    with DisabledOptionsSelect (core/widgets.py) so the full vocabulary and
    the "pending decision" fieldset descriptions stay visible on the form —
    matching D10's "rendered disabled with a 'pending decision' note so the
    screen layout is preserved" — while a normal browser cannot submit them.
    discounts.validators.validate_gift_card_campaign() is still the
    authoritative, defense-in-depth rejection for any POST that bypasses this
    widget.
    """

    class Meta:
        model = GiftCardCampaign
        fields = "__all__"
        widgets = {
            "trigger_scope": DisabledOptionsSelect(
                disabled_values=[GiftCardCampaignTriggerScope.CONDITIONS],
            ),
            "value_mode": DisabledOptionsSelect(
                disabled_values=[
                    GiftCardCampaignValueMode.PERCENT_OF_ORDER,
                    GiftCardCampaignValueMode.PERCENT_DISCOUNT_COUPON,
                ],
            ),
        }

    def clean(self):
        cleaned = super().clean()
        status = cleaned.get("status")
        trigger_scope = cleaned.get("trigger_scope")
        trigger_products = cleaned.get("trigger_products")
        if (
            status == GiftCardCampaignStatus.PUBLISHED
            and trigger_scope == GiftCardCampaignTriggerScope.MANUAL_PRODUCTS
            and not trigger_products
        ):
            raise forms.ValidationError({
                "trigger_products": (
                    "Select at least one product before publishing a "
                    "'Manually add products' campaign."
                ),
            })
        return cleaned


@admin.register(GiftCardCampaign, site=super_admin_site)
class GiftCardCampaignAdmin(admin.ModelAdmin):
    """
    Cross-store gift-card campaign admin (D10 super-admin-12 list + edit
    screens). issued_count/used_count/outstanding_count are model properties
    (computed queries, D8) — Django admin can reference them directly in
    list_display/readonly_fields without a wrapping method.

    D10 screen-parity polish (wave-3 spec review): name_link routes each row
    to the pre-filtered issued-cards DiscountCode changelist; status_dot
    renders the published/draft/archived color dot. The "(Paste default
    message)" affordance on the Delivery editor (a client-side JS
    interaction inserting the platform template body into email_body) is
    intentionally NOT implemented here — descoped, see ADR-029's Ticket
    scope section and this ticket's report.
    """

    form = GiftCardCampaignAdminForm
    list_display = (
        "name_link",
        "status_dot",
        "store",
        "issued_count",
        "used_count",
        "outstanding_count",
        "updated_at",
    )
    list_filter = ("status", "store")
    search_fields = ("name",)
    readonly_fields = (
        "issued_count",
        "used_count",
        "outstanding_count",
        "created_at",
        "updated_at",
    )
    actions = ["archive_selected"]
    fieldsets = (
        (
            "General",
            {"fields": ("store", "name", "status", "owner_scope")},
        ),
        (
            "Trigger",
            {
                "fields": ("trigger_event", "trigger_scope", "trigger_products"),
                "description": (
                    "'Products based on conditions' is reserved for a future release "
                    "(D2, PENDING human decision) — only 'All products' and "
                    "'Manually add products' are available in v1."
                ),
            },
        ),
        (
            "Gift card value",
            {
                "fields": ("value_mode", "value"),
                "description": (
                    "Only 'Set value' (fixed) is available in v1 — the percentage "
                    "modes are reserved for a future release (D3, PENDING human decision)."
                ),
            },
        ),
        (
            "Expiry",
            {
                "fields": ("expiry_mode", "expiry_days", "expiry_date"),
                "description": (
                    "Stores that target France are held to a statutory minimum "
                    "validity floor at publish time (ADR-029 D6)."
                ),
            },
        ),
        (
            "Frequency cap",
            {
                "fields": ("cap_enabled", "cap_count", "cap_window_value", "cap_window_unit"),
                "description": (
                    "Recommended: enable this cap (e.g. 1 gift card per 30 days). It is "
                    "the primary brake against a customer self-gifting by placing many "
                    "minimum-value orders (ADR-029 D5/D9). Left disabled (the default), "
                    "there is no per-customer issuance limit."
                ),
            },
        ),
        (
            "Budget cap",
            {
                "fields": ("max_issued_cards",),
                "description": (
                    "Recommended: set a limit. Left blank (unlimited, the default), an "
                    "'all products' campaign issues one gift card per qualifying order "
                    "with no ceiling on total liability."
                ),
            },
        ),
        (
            "Delivery",
            {
                "fields": ("email_subject", "email_body"),
                "description": (
                    "Leave blank to use the platform 'gift_card_campaign' email "
                    "template. Supports the '[DISCOUNT]' and "
                    "'[GENERATED GIFT CARD CODE]' token pills."
                ),
            },
        ),
        (
            "Counters",
            {
                "fields": ("issued_count", "used_count", "outstanding_count"),
                "classes": ("collapse",),
            },
        ),
        (
            "Meta",
            {"fields": ("created_at", "updated_at"), "classes": ("collapse",)},
        ),
    )

    @admin.action(description="Archive selected campaigns")
    def archive_selected(self, request, queryset):
        updated = queryset.update(status=GiftCardCampaignStatus.ARCHIVED)
        self.message_user(request, f"{updated} campaign(s) archived.")

    @admin.display(description="Name", ordering="name")
    def name_link(self, obj):
        """
        D10: "each campaign list row links to it [the issued-cards list]
        pre-filtered." Links to the DiscountCode changelist filtered by this
        campaign via GiftCardCampaignListFilter's parameter_name
        ('gift_card_campaign', a plain SimpleListFilter — NOT the
        '<field>__id__exact' shape Django's stock FK list_filter would use).
        """
        url = reverse(f"{self.admin_site.name}:discounts_discountcode_changelist")
        return format_html('<a href="{}?gift_card_campaign={}">{}</a>', url, obj.pk, obj.name)

    @admin.display(description="Status")
    def status_dot(self, obj):
        """Colored status dot (mirrors badges.admin.SecurityBadgeAdmin.status_dot)."""
        color = {
            GiftCardCampaignStatus.PUBLISHED: "#28a745",
            GiftCardCampaignStatus.DRAFT: "#999999",
            GiftCardCampaignStatus.ARCHIVED: "#6c757d",
        }.get(obj.status, "#999999")
        return format_html(
            '<span style="color:{};">&#9679;</span> {}', color, obj.get_status_display(),
        )

    def get_queryset(self, request):
        return GiftCardCampaign.objects.cross_store_unsafe()

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        """
        Scope the trigger_products widget to the campaign's own store on
        change (ADR-031: a super-admin editing one campaign should not be
        offered every other store's products in the picker). On the add form
        there is no instance yet to scope by, so every product remains
        available — mirrors catalog.admin.ProductPageVersionImageInline's
        formfield_for_foreignkey pattern (object_id from the URL, .none() as
        the safe default if lookup fails) and is safe because the new
        core/m2m_guard.py pre_add guard rejects any cross-store selection at
        save time regardless of what the widget offered.
        """
        if db_field.name == "trigger_products":
            from catalog.models import Product

            obj_id = request.resolver_match.kwargs.get("object_id") if request.resolver_match else None
            queryset = Product.objects.cross_store_unsafe()
            if obj_id:
                try:
                    campaign = GiftCardCampaign.objects.cross_store_unsafe().get(pk=obj_id)
                    queryset = Product.objects.for_store(campaign.store)
                except (GiftCardCampaign.DoesNotExist, ValueError, TypeError):
                    pass
            kwargs["queryset"] = queryset
        return super().formfield_for_manytomany(db_field, request, **kwargs)
