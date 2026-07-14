"""
Order admin registrations for Pradize (TICKET-010, T009/T010 Phase-1 remediation).

- OrderAdmin on store_admin_site: store-scoped order list + detail editor with
  read-only OrderItem snapshots and editable OrderFulfillment tracking.
  All querysets go through .for_store() or .cross_store_unsafe() — never
  .all() or .filter() directly (ADR-001 §4).
- OrderSuperAdmin on super_admin_site: cross-store read for super-admins.
- Module permission gate: employees need 'orders' limited to view, full to
  change payment_status / fulfillment_status / notes and to add fulfillments.
- OrderItem fields are always read-only — they are snapshots, never editable.
- CSV export action: exports selected orders to a downloadable CSV file.
- refund_order action: processes a full refund via the payment connector for
  PAID orders; guards against refunding beyond captured amount (ADR-007 §7).
- OrderAdminForm enforces state-transition rules in clean() to block backwards
  transitions (AC-060): PAID→PENDING/FAILED, FAILED→PENDING, SHIPPED→NOT_SENT.
"""

import csv
from decimal import Decimal

from django import forms
from django.contrib import admin, messages
from django.core.exceptions import ValidationError
from django.http import HttpResponse

from core.admin import StoreOwnedInlineMixin
from orders.models import (
    ChargeStatus,
    FulfillmentStatus,
    Order,
    OrderFulfillment,
    OrderItem,
    PaymentStatus,
)
from stores.permissions import check_module_access
from webecom.admin import store_admin_site, super_admin_site


# ---------------------------------------------------------------------------
# OrderAdminForm — state-transition guards (AC-060)
# ---------------------------------------------------------------------------

class OrderAdminForm(forms.ModelForm):
    """
    ModelForm for Order that enforces payment_status and fulfillment_status
    state-transition rules in clean().

    Forbidden transitions (AC-060):
    - payment_status:   PAID → PENDING or FAILED (backwards)
                        FAILED → PENDING (backwards)
    - fulfillment_status: SHIPPED → NOT_SENT (backwards)

    The guard runs only when editing an existing order (instance.pk is set).
    New orders have no prior state and are always allowed.
    """

    class Meta:
        model = Order
        fields = '__all__'

    def clean(self):
        cleaned_data = super().clean()
        if not self.instance.pk:
            return cleaned_data  # New order — no prior state to compare.

        # Fetch old values in one query to avoid N+1 reads.
        existing = (
            Order.objects.cross_store_unsafe()
            .filter(pk=self.instance.pk)
            .values('payment_status', 'fulfillment_status')
            .first()
        )
        if not existing:
            return cleaned_data

        old_payment = existing['payment_status']
        new_payment = cleaned_data.get('payment_status')
        old_fulfillment = existing['fulfillment_status']
        new_fulfillment = cleaned_data.get('fulfillment_status')

        # payment_status backwards-transition guards.
        if new_payment and new_payment != old_payment:
            if (
                old_payment == PaymentStatus.PAID
                and new_payment in (PaymentStatus.PENDING, PaymentStatus.FAILED)
            ):
                raise ValidationError(
                    f"Cannot move payment status from PAID to {new_payment}. "
                    "Use the refund action to issue a refund."
                )
            if (
                old_payment == PaymentStatus.FAILED
                and new_payment == PaymentStatus.PENDING
            ):
                raise ValidationError(
                    "Cannot move payment status from FAILED back to PENDING."
                )

        # fulfillment_status backwards-transition guard.
        if (
            new_fulfillment
            and new_fulfillment != old_fulfillment
            and old_fulfillment == FulfillmentStatus.SHIPPED
            and new_fulfillment == FulfillmentStatus.NOT_SENT
        ):
            raise ValidationError(
                "Cannot move fulfillment status from SHIPPED back to NOT_SENT."
            )

        return cleaned_data


# ---------------------------------------------------------------------------
# CSV export action
# ---------------------------------------------------------------------------

@admin.action(description="Export selected orders to CSV")
def export_orders_csv(modeladmin, request, queryset):
    """
    Download a CSV of the selected orders. Columns mirror the list_display
    fields plus currency for completeness.
    """
    response = HttpResponse(content_type="text/csv")
    response["Content-Disposition"] = 'attachment; filename="orders.csv"'
    writer = csv.writer(response)
    writer.writerow([
        "Order Number",
        "Email",
        "Total",
        "Currency",
        "Payment Status",
        "Fulfillment Status",
        "Created At",
    ])
    for order in queryset.select_related():
        writer.writerow([
            order.order_number,
            order.customer_email,
            order.total,
            order.currency,
            order.payment_status,
            order.fulfillment_status,
            order.created_at.isoformat(),
        ])
    return response


# ---------------------------------------------------------------------------
# Refund action (AC-005, AC-061)
# ---------------------------------------------------------------------------

@admin.action(description="Process full refund for selected orders")
def refund_order(modeladmin, request, queryset):
    """
    Issue a full refund for each selected PAID order via the payment connector.

    Refund cap (ADR-007 §7): captured amount (amount_cents / 100) minus the
    already-refunded amount (OrderCharge.refunded_amount).  Charges that are
    already fully refunded are skipped with a warning.

    The connector's refund() method receives the amount in cents (int) as required
    by the processor interface.  On success, refunded_amount is incremented and
    payment_status is set to REFUNDED.
    """
    for order in queryset:
        if order.payment_status != PaymentStatus.PAID:
            modeladmin.message_user(
                request,
                f"Order {order.order_number} is not PAID — skipped.",
                messages.WARNING,
            )
            continue

        charge = (
            order.charges
            .filter(status=ChargeStatus.CAPTURED)
            .order_by('-created_at')
            .first()
        )
        if not charge:
            modeladmin.message_user(
                request,
                f"No captured charge found for order {order.order_number}.",
                messages.ERROR,
            )
            continue

        # Refund cap: captured − already refunded (ADR-007 §7).
        captured_amount = Decimal(charge.amount_cents) / 100
        max_refund = captured_amount - charge.refunded_amount
        if max_refund <= 0:
            modeladmin.message_user(
                request,
                f"Order {order.order_number} is already fully refunded.",
                messages.WARNING,
            )
            continue

        # Convert to cents (int) for the connector interface.
        max_refund_cents = int(max_refund * 100)

        if not order.processor_account_id:
            modeladmin.message_user(
                request,
                f"Order {order.order_number} has no processor account configured.",
                messages.ERROR,
            )
            continue

        try:
            from payments.factory import get_connector

            connector = get_connector(order.processor_account)
            result = connector.refund(
                charge.processor_charge_id,
                amount=max_refund_cents,
            )
            if result.success:
                charge.refunded_amount += max_refund
                charge.save(update_fields=['refunded_amount'])
                order.payment_status = PaymentStatus.REFUNDED
                order.save(update_fields=['payment_status'])
                modeladmin.message_user(
                    request,
                    f"Order {order.order_number} refunded successfully.",
                    messages.SUCCESS,
                )
            else:
                modeladmin.message_user(
                    request,
                    f"Refund failed for order {order.order_number}: {result.error_message}",
                    messages.ERROR,
                )
        except Exception as exc:
            modeladmin.message_user(
                request,
                f"Refund error for order {order.order_number}: {exc}",
                messages.ERROR,
            )


# ---------------------------------------------------------------------------
# Inlines
# ---------------------------------------------------------------------------

class OrderItemInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Read-only inline for OrderItem snapshot fields.

    OrderItem columns are captured at order creation and must never be
    edited — historical financial and product data must remain stable
    (ADR-002 snapshot rule).
    """

    model = OrderItem
    extra = 0
    fields = [
        "product_name",
        "variant_title",
        "sku",
        "quantity",
        "unit_price",
        "discount_amount",
        "line_total",
    ]
    readonly_fields = [
        "product_name",
        "variant_title",
        "sku",
        "quantity",
        "unit_price",
        "discount_amount",
        "line_total",
    ]

    def has_add_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False


class OrderFulfillmentInline(StoreOwnedInlineMixin, admin.StackedInline):
    """
    Editable inline for OrderFulfillment tracking records.

    Store-admin staff with 'orders' full access can add fulfillment rows
    (tracking number, carrier, URL, shipped_at, status, notes).  Multiple
    fulfillment rows are allowed to support split shipments.

    line_items_json is shown as a read-only display field; it is populated
    by the fulfillment service or the import_tracking management command
    (T010), not hand-entered in the admin.
    """

    model = OrderFulfillment
    extra = 0
    fields = [
        "tracking_number",
        "carrier",
        "tracking_url",
        "shipped_at",
        "status",
        "notes",
        "line_items_json",
    ]
    readonly_fields = ["line_items_json"]


# ---------------------------------------------------------------------------
# Store-admin OrderAdmin
# ---------------------------------------------------------------------------

@admin.register(Order, site=store_admin_site)
class OrderAdmin(admin.ModelAdmin):
    module_key = "orders"

    form = OrderAdminForm

    list_display = [
        "order_number",
        "customer_email",
        "total",
        "payment_status",
        "fulfillment_status",
        "created_at",
    ]
    list_filter = ["payment_status", "fulfillment_status", "created_at"]
    search_fields = [
        "order_number",
        "customer_email",
        "customer_name",
        "items__product_name",
    ]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]
    # Fields always read-only regardless of access level: these are set by the
    # checkout service and must never be altered via the admin.
    readonly_fields = [
        "order_number",
        "idempotency_key",
        "subtotal",
        "discount_amount",
        "shipping_amount",
        "total",
        "currency",
        "customer_email",
        "customer_name",
        "shipping_address",
        "billing_address",
        "created_at",
        "updated_at",
        "utm_source",
        "utm_medium",
        "utm_campaign",
        "first_referrer",
        "landing_page",
        # New fields — set by checkout/routing/webhook services, not admin.
        "customer",
        "organization",
        "authorized_at",
        "captured_at",
        "placed_at",
        "card_last4",
        "customer_local_hour",
    ]
    inlines = [OrderItemInline, OrderFulfillmentInline]
    actions = [export_orders_csv, refund_order]

    fieldsets = [
        (
            "Order",
            {
                "fields": [
                    "order_number",
                    "payment_status",
                    "fulfillment_status",
                    "notes",
                ]
            },
        ),
        (
            "Customer",
            {
                "fields": [
                    "customer",
                    "customer_email",
                    "customer_name",
                    "phone",
                    "customer_local_hour",
                    "card_last4",
                    "shipping_address",
                    "billing_address",
                ]
            },
        ),
        (
            "Financials",
            {
                "fields": [
                    "subtotal",
                    "discount_amount",
                    "shipping_amount",
                    "total",
                    "currency",
                    "discount_code",
                    "organization",
                ]
            },
        ),
        (
            "Attribution",
            {
                "fields": [
                    "utm_source",
                    "utm_medium",
                    "utm_campaign",
                    "first_referrer",
                    "landing_page",
                ],
                "classes": ["collapse"],
            },
        ),
        (
            "Meta",
            {
                "fields": [
                    "idempotency_key",
                    "authorized_at",
                    "captured_at",
                    "placed_at",
                    "created_at",
                    "updated_at",
                ],
                "classes": ["collapse"],
            },
        ),
    ]

    # ------------------------------------------------------------------
    # Dynamic readonly: limited-access employees cannot change statuses
    # ------------------------------------------------------------------

    def get_readonly_fields(self, request, obj=None):
        """
        Employees with only 'limited' orders access can view but not alter
        payment_status, fulfillment_status, or notes.
        """
        readonly = list(self.readonly_fields)
        if not check_module_access(request, "orders", "full"):
            readonly += ["payment_status", "fulfillment_status", "notes"]
        return readonly

    # ------------------------------------------------------------------
    # Queryset scoping
    # ------------------------------------------------------------------

    def get_queryset(self, request):
        """
        Scope the order list to the current store.
        Returns an empty queryset when request.store is not set — ADR-001 §4:
        cross-store data must never appear on the store-admin surface.
        """
        if getattr(request, "store", None):
            return Order.objects.for_store(request.store)
        return Order.objects.none()

    # ------------------------------------------------------------------
    # Save — assign store on creation only
    # ------------------------------------------------------------------

    def save_model(self, request, obj, form, change):
        """
        Assign obj.store = request.store for new orders only.
        Never overwrite the store FK on updates — prevents store-switching attacks.

        State-transition validation is handled by OrderAdminForm.clean() before
        this method is called, so no duplicate validation is needed here.

        PAID transition: after saving, suppress abandoned-checkout sessions when an
        admin manually changes payment_status → PAID (T021 — ADR-010 Q5). Mirrors
        the webhook handler path; idempotent and best-effort (non-fatal on failure).
        """
        # Cache old payment_status before saving so we can detect the →PAID transition.
        old_payment_status = None
        if change and obj.pk:
            old_payment_status = (
                Order.objects.cross_store_unsafe()
                .filter(pk=obj.pk)
                .values_list("payment_status", flat=True)
                .first()
            )

        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)

        # Suppress abandoned-checkout sessions on manual →PAID transition.
        if obj.payment_status == PaymentStatus.PAID and old_payment_status != PaymentStatus.PAID:
            try:
                from campaigns.service import suppress_abandoned_checkout
                suppress_abandoned_checkout(obj)
            except Exception:
                import logging as _logging
                _logging.getLogger("orders.admin").warning(
                    "suppress_abandoned_checkout failed for order %s (non-fatal)",
                    obj.pk,
                    exc_info=True,
                )


# ---------------------------------------------------------------------------
# Super-admin OrderSuperAdmin — cross-store read
# ---------------------------------------------------------------------------

@admin.register(Order, site=super_admin_site)
class OrderSuperAdmin(admin.ModelAdmin):
    list_display = [
        "order_number",
        "store",
        "customer_email",
        "total",
        "payment_status",
        "created_at",
    ]
    list_filter = ["payment_status", "fulfillment_status", "store", "created_at"]
    search_fields = [
        "order_number",
        "customer_email",
        "customer_name",
    ]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]
    raw_id_fields = ["store"]
    readonly_fields = [
        "order_number",
        "idempotency_key",
        "subtotal",
        "discount_amount",
        "shipping_amount",
        "total",
        "currency",
        "customer_email",
        "customer_name",
        "shipping_address",
        "billing_address",
        "created_at",
        "updated_at",
        "customer",
        "organization",
        "authorized_at",
        "captured_at",
        "placed_at",
        "card_last4",
        "customer_local_hour",
    ]
    inlines = [OrderItemInline]

    def get_queryset(self, request):
        return Order.objects.cross_store_unsafe()

    def save_model(self, request, obj, form, change):
        """
        Suppress abandoned-checkout sessions when a super-admin manually marks an
        order PAID (T021 — ADR-010 Q5). Mirrors the store-admin and webhook paths.
        """
        old_payment_status = None
        if change and obj.pk:
            old_payment_status = (
                Order.objects.cross_store_unsafe()
                .filter(pk=obj.pk)
                .values_list("payment_status", flat=True)
                .first()
            )

        super().save_model(request, obj, form, change)

        if obj.payment_status == PaymentStatus.PAID and old_payment_status != PaymentStatus.PAID:
            try:
                from campaigns.service import suppress_abandoned_checkout
                suppress_abandoned_checkout(obj)
            except Exception:
                import logging as _logging
                _logging.getLogger("orders.admin").warning(
                    "suppress_abandoned_checkout failed for order %s (non-fatal)",
                    obj.pk,
                    exc_info=True,
                )
