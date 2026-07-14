"""
Catalog feeds admin (ADR-026 D8, AF-016, TICKET-033-B).

Store-admin only (/admin/) — one "Catalog feeds" section with one card per
REGISTERED provider (registry-driven, mirrors pixels/admin.py's admin-018
card grid): a new provider appears with zero admin-code changes.

Permission: AF-016 names the actor as "Store Admin or Employee with Apps —
Full access" (not the usual limited/full split other modules use) — module
gate: stores.permissions.StoreModulePermissionMixin, auto-applied at
registration (ADR-033 D3b), module_key="apps". The generic view=limited/
mutate=full mapping is equivalent to the old level="full"-for-everything
convention here, since "apps" only ever stores "full" or "none" — there is
no "limited" value to diverge on. The two custom views below (not
ModelAdmin permission methods) still call check_module_access directly.
"""

import logging

from django.contrib import admin, messages
from django.http import HttpResponseForbidden, HttpResponseRedirect
from django.template.response import TemplateResponse
from django.urls import path, reverse
from django.utils.translation import gettext_lazy as _

from stores.permissions import check_module_access

logger = logging.getLogger(__name__)
from webecom.admin import store_admin_site

from .checks import get_launch_warnings
from .models import FeedConfig, FeedTarget, FeedTargetStatus, StoreFeedToken
from .registry import all_providers
from .tasks import regenerate_dirty_feeds


# Status -> CSS class / banner colour for the admin card (ADR-026 D8: green
# up_to_date, amber regenerating/stale, red error).
_BANNER_CLASS = {
    FeedTargetStatus.UP_TO_DATE: "feeds-banner--ok",
    FeedTargetStatus.REGENERATING: "feeds-banner--warn",
    FeedTargetStatus.ERROR: "feeds-banner--error",
    FeedTargetStatus.PENDING: "feeds-banner--warn",
}


@admin.register(FeedConfig, site=store_admin_site)
class FeedConfigAdmin(admin.ModelAdmin):
    module_key = "apps"

    fields = ["provider", "is_enabled", "scope", "collections"]
    filter_horizontal = ["collections"]

    # admin-013/D8 card grid replaces the default table; see changelist_view().
    change_list_template = "admin/feeds/feedconfig/change_list.html"

    # ------------------------------------------------------------------
    # Queryset scoping + permissions (AF-016: Apps — Full access, no
    # limited tier — every view here requires the same 'full' level).
    # ------------------------------------------------------------------

    def get_queryset(self, request):
        store = getattr(request, "store", None)
        if store is None:
            return FeedConfig.objects.none()
        return FeedConfig.objects.for_store(store)

    def formfield_for_manytomany(self, db_field, request, **kwargs):
        """
        Scope the collections widget to the current store (ADR-031
        TICKET-050): without this, Django's ManyToManyField.formfield()
        defaults `queryset` to catalog.Collection's own raising
        StoreScopedManager (core/managers.py) — renders fine (the admin's
        filter_horizontal widget uses QuerySet.iterator(), which
        _RaisingQuerySet does not intercept) but crashes with IsolationError
        the instant a selection is POSTed and validated, and would otherwise
        offer every store's collections in the picker.
        """
        store = getattr(request, "store", None)
        if store is not None and db_field.name == "collections":
            from catalog.models import Collection

            kwargs["queryset"] = Collection.objects.for_store(store)
        return super().formfield_for_manytomany(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        """Auto-assign the store on creation — 'store' is never exposed in the form."""
        if not change and hasattr(request, "store"):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    # ------------------------------------------------------------------
    # Card grid changelist
    # ------------------------------------------------------------------

    def changelist_view(self, request, extra_context=None):
        """
        Build one card per REGISTERED provider (ADR-026 D8), regardless of
        whether a FeedConfig row exists yet for this store — a provider with
        zero rows still renders an "Enable" card.

        Reuses get_queryset(request) for the store-scoped FeedConfig rows and
        .for_store() for FeedTarget rows, so isolation matches every other
        view on this ModelAdmin. Still calls super().changelist_view() so
        list-level permission checks run exactly as before.
        """
        extra_context = dict(extra_context or {})
        store = getattr(request, "store", None)

        if store is None:
            extra_context["provider_cards"] = []
            extra_context["launch_warnings"] = []
            return super().changelist_view(request, extra_context=extra_context)

        configs_by_provider = {c.provider: c for c in self.get_queryset(request)}
        store_token = StoreFeedToken.get_or_create_for_store(store)

        cards = []
        for provider in all_providers():
            config = configs_by_provider.get(provider.key)

            targets = list(
                FeedTarget.objects.for_store(store)
                .filter(provider=provider.key)
                .select_related("store_language", "store_language__domain")
                .order_by("store_language__lang_code", "country_code")
            )
            target_rows = []
            for t in targets:
                url = (
                    f"https://{t.store_language.domain.host}/feeds/{provider.key}/"
                    f"{t.country_code.lower()}-{t.store_language.lang_code}.xml"
                    f"?token={store_token.token}"
                )
                banner_status = "stale" if t.is_stale else t.status
                target_rows.append({
                    "label": f"{t.country_code}-{t.store_language.lang_code}",
                    "status": t.status,
                    "banner_status": banner_status,
                    "banner_class": _BANNER_CLASS.get(t.status, "feeds-banner--warn"),
                    "url": url,
                    "item_count": t.item_count,
                    "excluded_count": t.excluded_count,
                    "exclusions": t.exclusions_json,
                    "last_generated_at": t.last_generated_at,
                    "last_error": t.last_error,
                })

            if config is not None:
                edit_url = reverse(f"{self.admin_site.name}:feeds_feedconfig_change", args=[config.pk])
                regenerate_url = reverse(
                    f"{self.admin_site.name}:feeds_feedconfig_regenerate", args=[config.pk]
                )
            else:
                edit_url = (
                    reverse(f"{self.admin_site.name}:feeds_feedconfig_add")
                    + f"?provider={provider.key}"
                )
                regenerate_url = None

            cards.append({
                "key": provider.key,
                "name": provider.name,
                "enabled": bool(config and config.is_enabled),
                "scope": config.get_scope_display() if config else "",
                "edit_url": edit_url,
                "regenerate_url": regenerate_url,
                "health": provider.health_check(store),
                "targets": target_rows,
            })

        extra_context["provider_cards"] = cards
        extra_context["launch_warnings"] = get_launch_warnings(store)
        extra_context["rotate_token_url"] = reverse(
            f"{self.admin_site.name}:feeds_feedconfig_rotate_token"
        )
        return super().changelist_view(request, extra_context=extra_context)

    # ------------------------------------------------------------------
    # Custom actions
    # ------------------------------------------------------------------

    def get_urls(self):
        custom_urls = [
            path(
                "<int:pk>/regenerate/",
                self.admin_site.admin_view(self.regenerate_view),
                name="feeds_feedconfig_regenerate",
            ),
            path(
                "rotate-token/",
                self.admin_site.admin_view(self.rotate_token_view),
                name="feeds_feedconfig_rotate_token",
            ),
        ]
        return custom_urls + super().get_urls()

    def regenerate_view(self, request, pk):
        """
        "Regenerate now" (ADR-026 D8): marks every FeedTarget row for this
        FeedConfig dirty and enqueues the SAME beat task the debounced
        schedule runs (feeds.tasks.regenerate_dirty_feeds) — generation never
        happens in-request (FeedProvider.run() docstring).
        """
        changelist_url = reverse(f"{self.admin_site.name}:feeds_feedconfig_changelist")

        try:
            config = self.get_queryset(request).get(pk=pk)
        except FeedConfig.DoesNotExist:
            return HttpResponseForbidden(_("Feed config not found or access denied."))

        if not self.has_change_permission(request, config):
            return HttpResponseForbidden(_("You do not have permission to change this feed."))

        if request.method == "POST":
            updated = (
                FeedTarget.objects.for_store(config.store)
                .filter(provider=config.provider)
                .update(is_dirty=True)
            )
            regenerate_dirty_feeds.delay()
            self.message_user(
                request,
                _("%(provider)s: %(count)d feed target(s) queued for regeneration.")
                % {"provider": config.get_provider_display(), "count": updated},
                level=messages.SUCCESS,
            )
            return HttpResponseRedirect(changelist_url)

        return TemplateResponse(
            request,
            "admin/feeds/feedconfig/regenerate_confirm.html",
            {
                **self.admin_site.each_context(request),
                "config": config,
                "changelist_url": changelist_url,
                "opts": FeedConfig._meta,
                "title": _("Regenerate Feed"),
            },
        )

    def rotate_token_view(self, request):
        """
        Store-level "Rotate feed token" (ADR-026 D2/D8, AC-213): replaces the
        one secret token shared by every provider's feed URLs. Rotation is
        instant-revoke — every previously copied URL stops working the
        moment this completes.
        """
        changelist_url = reverse(f"{self.admin_site.name}:feeds_feedconfig_changelist")
        store = getattr(request, "store", None)

        if store is None or not check_module_access(request, "apps", "full"):
            return HttpResponseForbidden(
                _("You do not have permission to rotate the feed token.")
            )

        if request.method == "POST":
            token = StoreFeedToken.get_or_create_for_store(store)
            token.rotate()
            # StoreFeedToken.rotate() already logs (store id + timestamp,
            # security audit F2); this adds the acting admin user, which
            # rotate() itself has no access to.
            logger.info(
                "feeds: token rotated for store %s by user %s", store.pk, request.user.pk
            )
            self.message_user(
                request,
                _(
                    "Feed token rotated. Every feed URL has changed — update the "
                    "data source in Google Merchant Center and Meta Business Manager "
                    "now; the old URLs stopped working immediately."
                ),
                level=messages.WARNING,
            )
            return HttpResponseRedirect(changelist_url)

        return TemplateResponse(
            request,
            "admin/feeds/feedconfig/rotate_token_confirm.html",
            {
                **self.admin_site.each_context(request),
                "changelist_url": changelist_url,
                "opts": FeedConfig._meta,
                "title": _("Rotate Feed Token"),
            },
        )
