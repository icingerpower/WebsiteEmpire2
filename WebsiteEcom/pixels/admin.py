"""
Pixel admin (ADR-022 D8, admin-018).

Store-admin only (/admin/) — pixels are a per-store configuration surface,
not a super-admin (platform-wide) concern. Mirrors the ShippingRate /
DiscountCode store-admin pattern: get_queryset scopes to request.store,
save_model force-assigns store on creation so a store-admin cannot spoof
another store's row.

admin-018 (spec 06 §9, ADR-022 D8) is the card-per-registry-provider
install/uninstall grid. changelist_view() below overlays that presentation
on top of the same get_queryset/save_model logic — the queryset scoping and
permission checks that existed before this pass are untouched; only the
template rendered by the changelist and the two extra confirm-style views
(uninstall) are new.
"""

from django.contrib import admin, messages
from django.http import HttpResponseForbidden, HttpResponseRedirect
from django.template.response import TemplateResponse
from django.urls import path, reverse
from django.utils.translation import gettext_lazy as _

from webecom.admin import store_admin_site

from .models import Pixel
from .registry import all_providers

#: Number of trailing characters of a pixel_id shown, uninverted, on the
#: admin-018 card (ADR-022 D8: "masked pixel id when installed"). Everything
#: before them is replaced by a fixed-width bullet run so cards for
#: differently-sized IDs (Facebook's 5-20 digit IDs vs Snapchat's 36-char
#: UUIDs) do not leak the ID's real length at a glance.
_PIXEL_ID_VISIBLE_SUFFIX = 4


def _mask_pixel_id(pixel_id):
    """Return pixel_id with everything but its last few characters bulleted out."""
    if not pixel_id:
        return ""
    if len(pixel_id) <= _PIXEL_ID_VISIBLE_SUFFIX:
        return pixel_id
    return "••••••" + pixel_id[-_PIXEL_ID_VISIBLE_SUFFIX:]


@admin.register(Pixel, site=store_admin_site)
class PixelAdmin(admin.ModelAdmin):
    module_key = "pixels"

    list_display = ["provider", "pixel_id", "is_active", "updated_at"]
    list_filter = ["provider", "is_active"]
    search_fields = ["pixel_id"]
    readonly_fields = ["created_at", "updated_at"]
    fields = ["provider", "pixel_id", "is_active", "config_json", "created_at", "updated_at"]

    # admin-018 card grid replaces the default table; see changelist_view().
    change_list_template = "admin/pixels/pixel/change_list.html"

    def get_queryset(self, request):
        """Scope to the requesting store. Returns empty queryset when store is absent."""
        store = getattr(request, "store", None)
        if store is None:
            return Pixel.objects.none()
        return Pixel.objects.for_store(store)

    def save_model(self, request, obj, form, change):
        """Auto-assign the store on creation — 'store' is never exposed in the form."""
        if not change and hasattr(request, "store"):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    def changelist_view(self, request, extra_context=None):
        """
        Build one card per REGISTERED provider (ADR-022 D8) — iterating
        all_providers() rather than the queryset means a provider with zero
        Pixel rows for this store still renders a "Not installed" card with
        an Install button, instead of being silently absent from the page.

        Reuses get_queryset(request) for the store-scoped rows, so a
        store-admin only ever sees their own store's pixel_id values — the
        same isolation guarantee as the rest of this ModelAdmin.

        This only adds template context; it does not change what get_queryset,
        save_model, or the standard add/change views do, and it still calls
        super().changelist_view() so list-level permission checks
        (has_view_or_change_permission etc.) run exactly as before.
        """
        extra_context = dict(extra_context or {})

        pixels_by_provider = {pixel.provider: pixel for pixel in self.get_queryset(request)}

        cards = []
        for provider in all_providers():
            pixel = pixels_by_provider.get(provider.key)
            installed = bool(pixel is not None and pixel.is_active)

            if pixel is not None:
                # A row already exists for this provider (installed or
                # previously uninstalled) — route to the standard change view.
                # Routing back to the add view here would hit the
                # (store, provider) unique_together constraint on save.
                change_url = reverse(
                    f"{self.admin_site.name}:pixels_pixel_change", args=[pixel.pk]
                )
                uninstall_url = reverse(
                    f"{self.admin_site.name}:pixels_pixel_uninstall", args=[pixel.pk]
                )
                primary_url = change_url
            else:
                change_url = None
                uninstall_url = None
                primary_url = (
                    reverse(f"{self.admin_site.name}:pixels_pixel_add")
                    + f"?provider={provider.key}"
                )

            cards.append(
                {
                    "key": provider.key,
                    "name": provider.name,
                    "id_label": provider.id_label,
                    "installed": installed,
                    "pixel_id_masked": _mask_pixel_id(pixel.pixel_id) if pixel else "",
                    "primary_url": primary_url,
                    "edit_url": change_url,
                    "uninstall_url": uninstall_url,
                }
            )

        extra_context["provider_cards"] = cards
        return super().changelist_view(request, extra_context=extra_context)

    def get_urls(self):
        custom_urls = [
            path(
                "<int:pk>/uninstall/",
                self.admin_site.admin_view(self.uninstall_view),
                name="pixels_pixel_uninstall",
            ),
        ]
        return custom_urls + super().get_urls()

    def uninstall_view(self, request, pk):
        """
        "..." menu → Uninstall (ADR-022 D8 / Pixel model docstring): sets
        is_active=False, the row is kept for audit. Deliberately NOT wired to
        admin.ModelAdmin's real delete_view — that permanently removes the
        row, which would break the audit trail the model explicitly documents
        as an invariant ("Uninstalling never deletes the row").

        GET renders a lightweight confirmation page (same two-step shape as
        campaigns.CampaignAdmin.activate_campaign_view); POST performs the
        toggle. Uses get_queryset(request) + has_change_permission so store
        scoping and permissions are identical to every other view on this
        ModelAdmin — no new isolation surface is introduced.
        """
        changelist_url = reverse(f"{self.admin_site.name}:pixels_pixel_changelist")

        try:
            pixel = self.get_queryset(request).get(pk=pk)
        except Pixel.DoesNotExist:
            return HttpResponseForbidden(_("Pixel not found or access denied."))

        if not self.has_change_permission(request, pixel):
            return HttpResponseForbidden(
                _("You do not have permission to change this pixel.")
            )

        if request.method == "POST":
            pixel.is_active = False
            pixel.save(update_fields=["is_active", "updated_at"])
            self.message_user(
                request,
                _("%(provider)s uninstalled.") % {"provider": pixel.get_provider_display()},
                level=messages.SUCCESS,
            )
            return HttpResponseRedirect(changelist_url)

        return TemplateResponse(
            request,
            "admin/pixels/pixel/uninstall_confirm.html",
            {
                **self.admin_site.each_context(request),
                "pixel": pixel,
                "changelist_url": changelist_url,
                "opts": Pixel._meta,
                "title": _("Uninstall Pixel"),
            },
        )
