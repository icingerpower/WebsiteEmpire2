"""
Cart admin registrations for Pradize (TICKET-010-CART).

CartItemInline: read-only snapshot of cart line items inside CartAdmin.
CartAdmin: registered on store_admin_site, store-scoped via get_queryset().

All querysets use .for_store() or .cross_store_unsafe() — never .all()
or .filter() directly (ADR-001 §4).
"""

from django.contrib import admin

from cart.models import Cart, CartItem
from core.admin import StoreOwnedInlineMixin
from webecom.admin import store_admin_site


class CartItemInline(StoreOwnedInlineMixin, admin.StackedInline):
    """
    Overrides StoreOwnedInlineMixin's default get_queryset() with a tighter
    .for_store(request.store) scope when the request carries a store —
    still inherits the mixin's StoreSafeInlineFormSet (INLINE-FORMSET-PK-
    ISOLATION fix) and cross_store_unsafe() fallback for the no-store case.
    """

    model = CartItem
    extra = 0
    readonly_fields = ['variant', 'quantity', 'unit_price', 'created_at']
    can_delete = False

    def has_add_permission(self, request, obj=None):
        return False

    def get_queryset(self, request):
        if hasattr(request, 'store') and request.store:
            return CartItem.objects.for_store(request.store).filter(
                cart__store=request.store
            )
        return CartItem.objects.cross_store_unsafe()


@admin.register(Cart, site=store_admin_site)
class CartAdmin(admin.ModelAdmin):
    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): no dedicated "carts" row in the 19-module
    # vocabulary; gated as an orders concern (carts are pre-order state).
    module_key = "orders"

    list_display = [
        'session_key_short',
        'store',
        'customer_email',
        'status',
        'currency',
        'expires_at',
        'created_at',
        'updated_at',
    ]
    list_filter = ['status', 'currency']
    search_fields = ['session_key', 'customer_email']
    readonly_fields = ['session_key', 'store', 'created_at', 'updated_at', 'expires_at']
    inlines = [CartItemInline]

    def session_key_short(self, obj):
        """Display the first 12 characters of the session key for readability."""
        return obj.session_key[:12] + '…'
    session_key_short.short_description = 'Session key'

    def get_queryset(self, request):
        if hasattr(request, 'store') and request.store:
            return Cart.objects.for_store(request.store)
        return Cart.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        """Carts are created programmatically, not via admin."""
        return False
