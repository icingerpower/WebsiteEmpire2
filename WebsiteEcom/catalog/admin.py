"""
Product admin registrations for Pradize (TICKET-004 / TICKET-024).

- ProductAdmin on store_admin_site: store-scoped product list + editor with
  variants (TabularInline) and images (TabularInline).  All querysets go
  through .for_store() or .cross_store_unsafe() — never .all() or .filter()
  directly (ADR-001 §4).
- ProductSuperAdmin on super_admin_site: cross-store read for super-admins.
- Module permission gate: store employees need 'products' access (limited to
  view, full to add/change/delete); super-admins always pass.

Phase 6 (TICKET-024) additions:
- TranslationStatusInline: read-only tabular inline showing existing translation
  records with status, AiJob link, manually_edited flag, and last-updated time.
- "translate_all_missing" bulk action: creates AiJobs for (product, language)
  pairs that have no published translation and no non-terminal job.
- ProductTranslationAdmin.save_model sets manually_edited_at when a human saves
  a translation via the admin form.
"""

from django import forms
from django.contrib import admin, messages
from django.db.models import Count
from django.http import HttpResponseForbidden
from django.template.response import TemplateResponse
from django.urls import path, reverse
from django.utils import timezone
from django.utils.html import format_html, mark_safe

from core.admin import StoreOwnedInlineMixin
from core.admin_widgets import StoreScopedForeignKeyRawIdWidget
from core.widgets import CsvListFormField, CsvListWidget
from stores.permissions import check_module_access
from catalog.models import (
    Collection,
    CollectionProduct,
    CollectionTranslation,
    Product,
    ProductImage,
    ProductPageVersion,
    ProductPageVersionImage,
    ProductTranslation,
    ProductVariant,
    QuotationRequest,
    StockNotification,
    TranslationStatus,
)
from media.admin_mixins import ImageThumbnailMixin
from webecom.admin import store_admin_site, super_admin_site


# ---------------------------------------------------------------------------
# Custom ModelForm
# ---------------------------------------------------------------------------

class ProductAdminForm(forms.ModelForm):
    """
    Custom ModelForm for ProductAdmin that adds a transient skip_redirect checkbox.

    skip_redirect is not a model field — it is read in save_model and forwarded
    to the slug_changed signal handler via obj._skip_redirect.  When True, the
    handler skips creating a SlugRedirect for the slug change.

    Useful when correcting a typo on a recently-added product that has no
    traffic to the old URL worth preserving.
    """

    skip_redirect = forms.BooleanField(
        required=False,
        initial=False,
        label="Skip redirect creation",
        help_text=(
            "If checked, changing the slug will NOT create a 301 redirect from "
            "the old URL.  Only check this if the old URL has no traffic worth "
            "preserving (e.g. fixing a typo on a brand-new product)."
        ),
    )

    class Meta:
        model = Product
        fields = "__all__"
        widgets = {
            "tags": CsvListWidget(attrs={"rows": 2, "placeholder": "tag1, tag2, tag3"}),
        }
        # ADR-031 Addendum 3 (D3a): CsvListWidget must pair with
        # CsvListFormField, not the stock JSONField — see core/widgets.py's
        # CsvListFormField docstring for the crash (and the pre-existing
        # display bug) this avoids.
        field_classes = {
            "tags": CsvListFormField,
        }


# ---------------------------------------------------------------------------
# Inlines
# ---------------------------------------------------------------------------

class ProductVariantInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Inline editor for ProductVariant inside the Product change page.
    TabularInline renders variants in a compact table — preferable when a product
    has multiple variants. StoreOwnedInlineMixin's cross_store_unsafe()
    convention is safe here because Django admin resolves the FK-relation
    queryset independently of the parent object; the variants already belong
    to the parent product and are safe to load here.
    """

    model = ProductVariant
    extra = 1
    fields = [
        "title",
        "sku",
        "price",
        "compare_at_price",
        "inventory_mode",
        "quantity",
        "weight_grams",
        "presale_ships_at",
        "is_default",
        "position",
        "is_active",
    ]


class ProductImageInline(StoreOwnedInlineMixin, ImageThumbnailMixin, admin.TabularInline):
    """
    Inline editor for ProductImage inside the Product change page.

    image_thumbnail is overridden here because ProductImage.image is an
    ImageField (not a URL string), so we derive the URL via obj.image.url.
    The parent mixin's implementation expects a plain URL attribute, which
    does not apply to an ImageField.
    """

    model = ProductImage
    extra = 0
    fields = ["image_thumbnail", "image", "alt_text", "display_order", "is_primary"]
    readonly_fields = ["image_thumbnail"]
    thumbnail_field = "image"

    def image_thumbnail(self, obj):
        if obj.image:
            return format_html(
                '<img src="{}" width="60" height="60" style="object-fit:cover;" />',
                obj.image.url,
            )
        return "—"

    image_thumbnail.short_description = "Preview"


# ---------------------------------------------------------------------------
# Translation inlines (TICKET-024 / Phase 6)
# ---------------------------------------------------------------------------

class TranslationStatusInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Read-only inline showing the translation status for each existing translation
    record on a Product.

    Columns: Language, Status, AiJob (link), Manually edited, Last updated.
    This inline is display-only — editors click the "Edit" link (ai_job FK)
    to go to the dedicated ProductTranslation admin form, or use the
    "Translate all missing" bulk action on the product changelist.

    manually_edited_at is shown as a boolean (was this ever manually edited?)
    and last updated is the updated_at timestamp.
    """

    model = ProductTranslation
    extra = 0
    can_delete = False
    fields = [
        "lang_code",
        "status",
        "ai_job",
        "is_manually_edited",
        "updated_at",
    ]
    readonly_fields = [
        "lang_code",
        "status",
        "ai_job",
        "is_manually_edited",
        "updated_at",
    ]
    verbose_name = "translation status"
    verbose_name_plural = "translation statuses"

    def has_add_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def is_manually_edited(self, obj):
        """Display whether this translation was ever manually edited by a human."""
        return obj.manually_edited_at is not None

    is_manually_edited.boolean = True
    is_manually_edited.short_description = "Manually edited"

    def get_queryset(self, request):
        # StoreOwnedInlineMixin supplies cross_store_unsafe(); chain the
        # extra select_related() this inline needs on top of it.
        return super().get_queryset(request).select_related("ai_job")


class CollectionTranslationInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Inline editor for CollectionTranslation rows inside the Collection change page.

    Follows the same read-only display pattern as TranslationStatusInline.
    """

    model = CollectionTranslation
    extra = 0
    can_delete = False
    fields = ["lang_code", "status", "ai_job", "updated_at"]
    readonly_fields = ["lang_code", "status", "ai_job", "updated_at"]

    def has_add_permission(self, request, obj=None):
        return False

    def has_change_permission(self, request, obj=None):
        return False


# ---------------------------------------------------------------------------
# "Translate all missing" bulk action (Phase 6)
# ---------------------------------------------------------------------------

def translate_all_missing(modeladmin, request, queryset):
    """
    Bulk action: create AiJobs for every (product, language) pair that has no
    published translation and no non-terminal (NOT_STARTED or IN_PROGRESS) job.

    Uses the flood guard from catalog/signals.py to avoid duplicate jobs.
    """
    from aijobs.models import AiJob, AiJobStatus
    from aijobs.service import create_job
    from catalog.ai_jobs import TRANSLATION_JOB_TYPE
    from catalog.signals import (
        _active_non_default_languages,
        _build_product_payload,
        _flood_guard,
        _get_default_lang,
    )

    created_count = 0
    store = getattr(request, "store", None)
    if store is None:
        modeladmin.message_user(request, "Cannot determine store for this request.", level=messages.ERROR)
        return

    lang_codes = _active_non_default_languages(store)
    if not lang_codes:
        modeladmin.message_user(request, "No non-default languages configured for this store.", level=messages.WARNING)
        return

    source_lang = _get_default_lang(store)

    for product in queryset.select_related("store"):
        for lang_code in lang_codes:
            # Skip if already published.
            if ProductTranslation.objects.cross_store_unsafe().filter(
                store=store,
                product=product,
                lang_code=lang_code,
                status=TranslationStatus.PUBLISHED,
            ).exists():
                continue
            # Skip if a non-terminal job already exists (flood guard).
            if _flood_guard(store, TRANSLATION_JOB_TYPE, "catalog.Product", product.pk, lang_code):
                continue
            payload = _build_product_payload(product, source_lang, lang_code)
            create_job(
                store=store,
                job_type=TRANSLATION_JOB_TYPE,
                target_model="catalog.Product",
                target_id=product.pk,
                input_payload=payload,
                lang=lang_code,
                created_by="human",
            )
            created_count += 1

    modeladmin.message_user(
        request,
        f"Created {created_count} translation job(s).",
        level=messages.SUCCESS if created_count else messages.WARNING,
    )


translate_all_missing.short_description = "Translate all missing languages"


# ---------------------------------------------------------------------------
# Store-admin ProductAdmin
# ---------------------------------------------------------------------------

@admin.register(Product, site=store_admin_site)
class ProductAdmin(admin.ModelAdmin):
    module_key = "products"

    form = ProductAdminForm
    list_display = [
        "title", "status", "translated_urls_html", "slug_conflicts_html",
        "page_versions_html", "published_at", "updated_at",
    ]
    list_filter = ["status"]
    search_fields = ["title", "slug", "tags"]
    prepopulated_fields = {"slug": ("title",)}
    readonly_fields = ["published_at", "created_at", "updated_at"]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]
    actions = [translate_all_missing]
    inlines = [ProductVariantInline, ProductImageInline, TranslationStatusInline]

    fieldsets = [
        (
            "Basic Info",
            {
                "fields": ["title", "slug", "skip_redirect", "status", "published_at"],
            },
        ),
        (
            "Content",
            {
                "fields": ["description", "tags", "vendor", "product_type"],
            },
        ),
        (
            "Video",
            {
                "fields": ["video_url"],
            },
        ),
        (
            "Shipping",
            {
                "fields": ["requires_shipping"],
            },
        ),
        (
            "SEO",
            {
                "fields": ["seo_title", "seo_description"],
                "classes": ["collapse"],
            },
        ),
        (
            "Meta",
            {
                "fields": ["created_at", "updated_at"],
                "classes": ["collapse"],
            },
        ),
    ]

    # ------------------------------------------------------------------
    # List-display helpers: translated URLs and slug-collision warnings
    # ------------------------------------------------------------------

    def translated_urls_html(self, obj):
        """
        Show the active Permalink URL(s) for this product, grouped by language.

        Displays one line per (lang, slug) pair so store admins can verify which
        translated URLs are live.  The primary-language permalink is included so
        the column is useful even without translations.
        """
        from django.contrib.contenttypes.models import ContentType
        from permalinks.models import Permalink

        ct = ContentType.objects.get_for_model(Product)
        entries = list(
            Permalink.objects.cross_store_unsafe()
            .filter(content_type=ct, object_id=obj.pk, is_active=True)
            .values_list("lang", "slug")
            .order_by("lang")
        )
        if not entries:
            return "—"
        parts = [
            format_html('<span>{}: /{}/</span>', lang, slug)
            for lang, slug in entries
        ]
        return mark_safe("<br>".join(str(p) for p in parts))

    translated_urls_html.short_description = "URLs"
    translated_urls_html.allow_tags = True

    def slug_conflicts_html(self, obj):
        """
        Warn when any of this product's translations share a slug_hint with another
        product's translation in the same store + language.

        A collision means two products would claim the same translated URL.  The
        store admin must resolve the conflict by editing one of the Permalink slugs.

        Returns a warning badge listing conflicted (lang, slug_hint) pairs, or "—"
        when no conflicts exist.

        Performance: one queryset per product row; acceptable for admin use.
        """
        # Find all (lang_code, slug_hint) pairs for THIS product that are non-empty.
        my_hints = list(
            ProductTranslation.objects.cross_store_unsafe()
            .filter(product=obj, store=obj.store)
            .exclude(slug_hint="")
            .values_list("lang_code", "slug_hint")
        )
        if not my_hints:
            return "—"

        conflicts = []
        for lang_code, slug_hint in my_hints:
            # Count other products in the same store+lang with the same slug_hint.
            collision_count = (
                ProductTranslation.objects.cross_store_unsafe()
                .filter(store=obj.store, lang_code=lang_code, slug_hint=slug_hint)
                .exclude(product=obj)
                .count()
            )
            if collision_count:
                conflicts.append(
                    format_html(
                        "⚠️ {}: {!r} ({} conflict{})",
                        lang_code,
                        slug_hint,
                        collision_count,
                        "s" if collision_count != 1 else "",
                    )
                )

        if not conflicts:
            return "—"
        return mark_safe("<br>".join(str(c) for c in conflicts))

    slug_conflicts_html.short_description = "Slug conflicts"
    slug_conflicts_html.allow_tags = True

    def page_versions_html(self, obj):
        """
        Show the count of A/B page versions for this product, linking to the
        filtered ProductPageVersion changelist (TICKET-042, ADR-028 §7).

        AF-12's toggle ("Create multiple URL variants of this product page") is
        simply the presence of active versions here — no separate store-level
        feature flag (ADR-028 §7).
        """
        count = ProductPageVersion.objects.cross_store_unsafe().filter(product=obj).count()
        if not count:
            return "—"
        url = f"/admin/catalog/productpageversion/?product__id__exact={obj.pk}"
        report_url = reverse(f"{self.admin_site.name}:catalog_product_page_versions_report", args=[obj.pk])
        return format_html(
            '<a href="{}">{} version{}</a> · <a href="{}">report</a>',
            url, count, "s" if count != 1 else "", report_url,
        )

    page_versions_html.short_description = "A/B page versions"
    page_versions_html.allow_tags = True

    # ------------------------------------------------------------------
    # Queryset scoping
    # ------------------------------------------------------------------

    def get_queryset(self, request):
        """
        Scope the product list to the current store.
        Returns an empty queryset when request.store is not set — ADR-001 §4:
        cross-store data must never appear on the store-admin surface.
        """
        if getattr(request, "store", None):
            return Product.objects.for_store(request.store)
        return Product.objects.none()

    # ------------------------------------------------------------------
    # Save — assign store on creation only
    # ------------------------------------------------------------------

    def save_model(self, request, obj, form, change):
        """
        Assign obj.store = request.store for new products only.
        Never overwrite the store FK on updates — prevents store-switching attacks.

        Slug-change guard (ADR-005 §6, T004): if an existing published product's
        slug is changed after >=7 days, the save is gated behind a confirmation
        field 'slug_change_confirm' in the POST data.  If the confirmation is absent,
        the slug is reverted and a warning message is shown.

        skip_redirect (Finding #12): reads the transient skip_redirect checkbox from
        the form and passes it to the slug_changed signal handler via obj._skip_redirect.
        When True the handler skips creating a SlugRedirect for the slug change.
        """
        if not change and getattr(request, "store", None):
            obj.store = request.store

        # Read skip_redirect from form.cleaned_data if it is a real dict (not a
        # MagicMock from unit tests).  Default to False so existing tests are unaffected.
        cleaned_data = (
            form.cleaned_data
            if isinstance(getattr(form, "cleaned_data", None), dict)
            else {}
        )
        skip_redirect = cleaned_data.get("skip_redirect", False)

        if change and obj.pk:
            original = Product.objects.for_store(obj.store).get(pk=obj.pk)
            if original.slug != obj.slug:
                days_published = (
                    (timezone.now() - original.published_at).days
                    if original.published_at
                    else 0
                )
                if days_published >= 7:
                    confirmed = request.POST.get("slug_change_confirm") == "1"
                    if not confirmed:
                        messages.warning(
                            request,
                            (
                                f"Slug change on a {days_published}-day-old published "
                                "product requires confirmation. Re-save with the "
                                "confirmation checkbox checked to proceed."
                            ),
                        )
                        obj.slug = original.slug  # revert until confirmed

        # Forward the flag to the slug_changed signal handler via the instance.
        # The handler checks _skip_redirect before writing the SlugRedirect row.
        obj._skip_redirect = skip_redirect

        super().save_model(request, obj, form, change)

    # ------------------------------------------------------------------
    # Formset save — propagate store FK to inline rows (TICKET-024)
    # ------------------------------------------------------------------

    def save_formset(self, request, form, formset, change):
        """
        Propagate the parent Product's store to each inline instance that carries
        a store FK but has none set yet.

        Without this override, new ProductTranslation rows added via the inline
        would hit a NOT NULL constraint on the store column because Django does not
        automatically copy the parent's store FK to inline children.

        The store is taken from form.instance (the already-saved Product) rather
        than request.store so that this is safe even in contexts where request.store
        is unavailable (e.g. super-admin, management commands).
        """
        instances = formset.save(commit=False)
        for instance in instances:
            if hasattr(instance, "store_id") and not instance.store_id:
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()

    # ------------------------------------------------------------------
    # A/B page-version comparison report (TICKET-042, ADR-028 §6)
    # ------------------------------------------------------------------

    def get_urls(self):
        custom_urls = [
            path(
                "<int:pk>/page-versions-report/",
                self.admin_site.admin_view(self.page_versions_report_view),
                name="catalog_product_page_versions_report",
            ),
        ]
        return custom_urls + super().get_urls()

    def page_versions_report_view(self, request, pk):
        """
        Minimal comparison report: raw counts per version (views, add-to-carts,
        orders, units, revenue, conversion rate) — TICKET-042, ADR-028 §6 v1.

        Gated by the REPORTS/ANALYTICS module permission (ADR-028 §7: "the
        results report sits under the reports/analytics module permission"),
        not the catalog "products" permission used for version CRUD — a store
        employee with read-only reporting access should see this without
        needing product-edit rights.
        """
        if not check_module_access(request, "analytics", "limited"):
            return HttpResponseForbidden("You do not have permission to view this report.")

        store = getattr(request, "store", None)
        if store is None:
            return HttpResponseForbidden("Store not found.")

        try:
            product = Product.objects.for_store(store).get(pk=pk)
        except Product.DoesNotExist:
            return HttpResponseForbidden("Product not found or access denied.")

        from catalog.page_version_reports import build_page_version_report

        days = 30
        try:
            days = int(request.GET.get("days", 30))
        except (TypeError, ValueError):
            pass
        days = max(1, min(365, days))

        rows = build_page_version_report(store, product, days=days)

        return TemplateResponse(
            request,
            "admin/catalog/product/page_versions_report.html",
            {
                **self.admin_site.each_context(request),
                "product": product,
                "rows": rows,
                "days": days,
                "opts": Product._meta,
                "title": f"A/B page versions — {product.title}",
                "changelist_url": reverse(f"{self.admin_site.name}:catalog_product_changelist"),
            },
        )


# ---------------------------------------------------------------------------
# ProductTranslation admin — for human review and manual editing (Phase 6)
# ---------------------------------------------------------------------------

@admin.register(ProductTranslation, site=store_admin_site)
class ProductTranslationAdmin(admin.ModelAdmin):
    """
    Store-admin view for individual ProductTranslation records.

    save_model sets manually_edited_at = now() whenever a human saves a
    translation via this form — regardless of which fields changed.  This
    ensures that AI-produced translations that were subsequently edited are
    marked and not silently overwritten by the next AI run (ADR-014 §4).

    persist_output() from catalog/ai_jobs.py NEVER calls this admin; it calls
    update_or_create() directly and leaves manually_edited_at untouched.
    """

    module_key = "products"

    list_display = ["product", "lang_code", "status", "slug_hint", "manually_edited_at", "updated_at"]
    list_filter = ["status", "lang_code"]
    search_fields = ["product__title", "lang_code"]
    readonly_fields = ["ai_job", "created_at", "updated_at"]
    fields = [
        "product",
        "lang_code",
        "title",
        "description",
        "seo_title",
        "seo_description",
        "slug_hint",
        "status",
        "ai_job",
        "manually_edited_at",
        "created_at",
        "updated_at",
    ]

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ProductTranslation.objects.for_store(request.store)
        return ProductTranslation.objects.none()

    def save_model(self, request, obj, form, change):
        """
        Set manually_edited_at to now() when a human saves via this form.

        This marks the translation as having been human-reviewed/edited so that
        automated re-runs know not to blindly overwrite it.  The field is set on
        every save through this admin — even if only the status was toggled —
        because the human exercised editorial judgment regardless of which field
        changed.
        """
        obj.manually_edited_at = timezone.now()
        super().save_model(request, obj, form, change)


# ---------------------------------------------------------------------------
# ProductPageVersion admin — A/B page versions (TICKET-042, ADR-028 §7)
# ---------------------------------------------------------------------------
#
# Registered as a DEDICATED ModelAdmin (not a TabularInline on ProductAdmin):
# Django admin does not support a second level of inline nesting
# (Product -> ProductPageVersion -> ProductPageVersionImage), and each version's
# image picker needs its own per-version ordering UI. ProductAdmin links to the
# filtered changelist here via page_versions_html above (ADR-028 §7 "inline or
# dedicated — per the ADR").


class ProductPageVersionImageInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Inline image picker for a ProductPageVersion — an ORDERED SUBSET of the
    parent product's own ProductImage rows (ADR-028 §1: no second binary store,
    no per-version alt text).

    The `image` dropdown is restricted to the current version's product images
    (formfield_for_foreignkey below). On the "add" form (no object yet), the
    dropdown is empty until the version is saved once — a known Django admin
    limitation for a doubly-nested picker; store admins add images on the
    following edit (ASSUMPTION, LOW).
    """

    model = ProductPageVersionImage
    extra = 1
    fields = ["image", "display_order"]

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        if db_field.name == "image":
            obj_id = request.resolver_match.kwargs.get("object_id") if request.resolver_match else None
            queryset = ProductImage.objects.none()
            if obj_id:
                try:
                    version = ProductPageVersion.objects.cross_store_unsafe().get(pk=obj_id)
                    queryset = ProductImage.objects.cross_store_unsafe().filter(product_id=version.product_id)
                except (ProductPageVersion.DoesNotExist, ValueError, TypeError):
                    pass
            kwargs["queryset"] = queryset
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


@admin.register(ProductPageVersion, site=store_admin_site)
class ProductPageVersionAdmin(admin.ModelAdmin):
    """
    Store-admin CRUD for A/B page versions (TICKET-042, ADR-028 §7).

    Gated by the SAME catalog module permission as ProductAdmin ("products") —
    versions are a product-editing concern, not a separate module.

    Cap-10 and reserved-slug-composition/collision enforcement live in
    ProductPageVersion.clean() and .save() (spec review PV-SLUG-COLLISION-500),
    so violations surface as a normal form error here, never a 500 (ADR-028 §2
    edge case).

    Activation-requires->=1-image (P3) is enforced in save_related, AFTER the
    image inline formset has saved, because the images do not exist yet at
    save_model time on first creation (ASSUMPTION, LOW).

    ADR-028 §7 admin conveniences:
    - url_previews_html: readonly per-language composed-URL preview on the
      change form (computed server-side — no live JS preview in v1, per the
      spec review's "readonly version is fine"). Clickable when the Permalink
      is already live; plain text otherwise.
    - delete_view: injects a 301-redirect warning into extra_context, rendered
      by templates/admin/catalog/productpageversion/delete_confirmation.html —
      same mechanism as campaigns.admin.CampaignStepAdmin.delete_view (the
      lighter of the two options: no ModelAdmin.get_deleted_objects override).
    """

    module_key = "products"

    list_display = ["product", "name", "slug_suffix", "is_active", "image_count", "position"]
    list_filter = ["is_active"]
    search_fields = ["name", "slug_suffix", "product__title"]
    raw_id_fields = ["product"]
    ordering = ["product", "position", "pk"]
    inlines = [ProductPageVersionImageInline]
    readonly_fields = ["created_at", "url_previews_html"]
    fields = ["product", "name", "slug_suffix", "is_active", "position", "url_previews_html", "created_at"]

    def image_count(self, obj):
        return obj.images.count()

    image_count.short_description = "Images"

    def url_previews_html(self, obj):
        """
        Readonly per-language composed-URL preview (ADR-028 §7). For every
        language the product has an active permalink in, shows the composed
        f'{product_slug}-{suffix}' URL:
        - a clickable link to the live storefront URL when this version's own
          Permalink for that language is already active (also serves as the
          "per-language preview link" requirement), or
        - the plain preview text when it is not (new/unsaved version, or a
          language added since the version was last synced).

        Uses permalinks.resolver.base_url() — "the ONLY place scheme + host +
        language prefix are composed" (ADR-005 §3 single-resolver principle) —
        never builds URLs inline.
        """
        if not obj or not obj.pk or not obj.product_id:
            return "(save this version to preview its composed URLs)"

        from django.contrib.contenttypes.models import ContentType

        from core.slugs import make_slug
        from permalinks.models import Permalink
        from permalinks.resolver import base_url
        from stores.models import StoreLanguage

        store = obj.product.store
        product_ct = ContentType.objects.get_for_model(Product)
        version_ct = ContentType.objects.get_for_model(ProductPageVersion)

        product_permalinks = list(
            Permalink.objects.for_store(store)
            .filter(content_type=product_ct, object_id=obj.product_id, is_active=True)
            .order_by("lang")
        )
        if not product_permalinks:
            return "—"

        normalized_suffix = make_slug(obj.slug_suffix) if obj.slug_suffix else ""
        store_languages = {
            sl.lang_code: sl
            for sl in StoreLanguage.objects.select_related("domain").filter(store=store)
        }
        own_permalinks = {
            p.lang: p
            for p in Permalink.objects.for_store(store).filter(
                content_type=version_ct, object_id=obj.pk, is_active=True,
            )
        }

        parts = []
        for pp in product_permalinks:
            composed = f"{pp.slug}-{normalized_suffix}" if normalized_suffix else pp.slug
            store_language = store_languages.get(pp.lang)
            own = own_permalinks.get(pp.lang)
            if own and store_language:
                url = f"{base_url(store_language)}/{own.slug}/"
                parts.append(format_html('{}: <a href="{}" target="_blank">{}</a>', pp.lang, url, url))
            elif store_language:
                preview_url = f"{base_url(store_language)}/{composed}/"
                parts.append(format_html("{}: {} (not yet live)", pp.lang, preview_url))
            else:
                parts.append(format_html("{}: /{}/ (not yet live)", pp.lang, composed))

        return mark_safe("<br>".join(str(p) for p in parts))

    url_previews_html.short_description = "Composed URLs per language"

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return ProductPageVersion.objects.for_store(request.store).select_related("product")
        return ProductPageVersion.objects.none()

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        Restrict the 'product' dropdown to the current store's products, and
        use StoreScopedForeignKeyRawIdWidget instead of the stock
        ForeignKeyRawIdWidget (see core/admin_widgets.py — the stock widget
        500s when this raw_id field re-renders with a bound value, e.g. on
        any validation error, because it looks up the label via the raising
        StoreScopedManager default manager; RAW-ID-WIDGET-ISOLATION).
        """
        if db_field.name == "product":
            if getattr(request, "store", None):
                kwargs["queryset"] = Product.objects.for_store(request.store)
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

    def save_model(self, request, obj, form, change):
        # store must always match the chosen product's store (no store-switching
        # attack surface — the product dropdown is already scoped above).
        if obj.product_id:
            obj.store = obj.product.store
        super().save_model(request, obj, form, change)

    def save_related(self, request, form, formsets, change):
        """
        Enforce "activation requires >= 1 image" (P3, ASSUMPTION LOW) AFTER the
        image inline has saved — images cannot exist yet at save_model time on
        first creation. Self-healing: deactivating here (rather than blocking
        the save outright) lets a store admin create the version, add images,
        then re-activate, without a confusing "add images first" 400 on save.
        """
        super().save_related(request, form, formsets, change)
        obj = form.instance
        # ADR-031 addendum audit (TICKET-051): `obj.images` is a reverse-FK
        # related manager (ProductPageVersionImage.page_version ->
        # ProductPageVersion) — ADR-031 deliberately leaves those raising
        # (only relation-bound M2M managers were relaxed). `.exists()` used
        # to silently bypass that raise entirely; scoped explicitly now that
        # it raises unconditionally.
        has_images = ProductPageVersionImage.objects.for_store(obj.store).filter(
            page_version=obj
        ).exists()
        if obj.is_active and not has_images:
            obj.is_active = False
            obj.save(update_fields=["is_active"])
            messages.warning(
                request,
                f"“{obj.name}” requires at least one image to be active "
                "(ADR-028) — deactivated automatically. Add at least one image, "
                "then re-activate.",
            )

    def delete_view(self, request, object_id, extra_context=None):
        """
        Extend the default delete confirmation with the ADR-028 §7 warning that
        deleting a page version permanently (301) redirects its live URL(s) to
        the primary product page (permalinks.registry.delete_page_version_permalinks,
        wired to the post_delete signal — unchanged by this method).

        Mirrors campaigns.admin.CampaignStepAdmin.delete_view: extra_context is
        rendered by a custom delete_confirmation.html template (the lighter
        mechanism — no ModelAdmin.get_deleted_objects override needed).
        """
        extra_context = dict(extra_context or {})
        try:
            version = ProductPageVersion.objects.cross_store_unsafe().get(pk=object_id)
            from django.contrib.contenttypes.models import ContentType

            from permalinks.models import Permalink

            version_ct = ContentType.objects.get_for_model(ProductPageVersion)
            active_permalinks = list(
                Permalink.objects.cross_store_unsafe()
                .filter(content_type=version_ct, object_id=version.pk, is_active=True)
                .order_by("lang")
            )
            extra_context["page_version_active_permalinks"] = active_permalinks
            extra_context["has_active_page_version_permalinks"] = bool(active_permalinks)
        except (ProductPageVersion.DoesNotExist, ValueError):
            pass
        return super().delete_view(request, object_id, extra_context=extra_context)


# ---------------------------------------------------------------------------
# Super-admin ProductSuperAdmin — cross-store read
# ---------------------------------------------------------------------------

@admin.register(Product, site=super_admin_site)
class ProductSuperAdmin(admin.ModelAdmin):
    list_display = ["title", "store", "status", "published_at", "updated_at"]
    list_filter = ["status", "store"]
    search_fields = ["title", "slug"]
    raw_id_fields = ["store"]
    readonly_fields = ["published_at", "created_at", "updated_at"]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]

    def get_queryset(self, request):
        return Product.objects.cross_store_unsafe()

    def has_add_permission(self, request):
        """
        Products must be created via store-admin, not super-admin.
        Super-admin can view and edit existing products but cannot add new ones.
        """
        return False


# ---------------------------------------------------------------------------
# Collection inline (manual product membership)
# ---------------------------------------------------------------------------

class CollectionProductInline(StoreOwnedInlineMixin, admin.TabularInline):
    """
    Inline editor for manual CollectionProduct memberships.

    StoreOwnedInlineMixin's cross_store_unsafe() convention is safe here
    because Django admin resolves the FK-relation queryset independently of
    the parent object; the memberships already belong to the parent
    collection and are safe to load here.
    """

    model = CollectionProduct
    extra = 0
    fields = ["product", "sort_key"]
    raw_id_fields = ["product"]
    ordering = ["sort_key"]

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        """
        'product' targets catalog.Product (StoreOwnedModel); use the safe
        raw-id widget so label_and_url_for_value() doesn't crash via the
        raising default manager the instant this inline re-renders with a
        bound value (RAW-ID-WIDGET-ISOLATION, core/admin_widgets.py). Does
        not change the field's queryset/selection scoping.
        """
        if db_field.name == "product":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)


# ---------------------------------------------------------------------------
# Store-admin CollectionAdmin
# ---------------------------------------------------------------------------

@admin.register(Collection, site=store_admin_site)
class CollectionAdmin(admin.ModelAdmin):
    # ADR-033 D1b: "collections" was merged into "products" (max-rank data
    # migration on StoreEmployee.permissions_json) — Collection is gated
    # under the SAME "products" key as ProductAdmin, not a standalone
    # "collections" key (which no longer exists in stores.modules.MODULES).
    module_key = "products"

    list_display = ["title", "collection_type", "is_published", "updated_at"]
    list_filter = ["collection_type", "is_published"]
    search_fields = ["title", "slug"]
    prepopulated_fields = {"slug": ("title",)}
    readonly_fields = ["created_at", "updated_at"]
    inlines = [CollectionProductInline, CollectionTranslationInline]

    fieldsets = [
        (
            None,
            {
                "fields": [
                    "title",
                    "slug",
                    "description",
                    "collection_type",
                    "is_published",
                    "image",
                ]
            },
        ),
        (
            "Smart Rules",
            {
                "fields": ["smart_rules", "smart_rules_match"],
                "classes": ["collapse"],
                "description": (
                    "Only used when Collection Type is \"smart\". "
                    "Rules format: "
                    '[{"field":"title","op":"contains","value":"..."}, ...]'
                ),
            },
        ),
        (
            "SEO",
            {
                "fields": ["seo_title", "seo_description"],
                "classes": ["collapse"],
            },
        ),
        (
            "Meta",
            {
                "fields": ["created_at", "updated_at"],
                "classes": ["collapse"],
            },
        ),
    ]

    # ------------------------------------------------------------------
    # Queryset scoping
    # ------------------------------------------------------------------

    def get_queryset(self, request):
        """
        Scope the collection list to the current store.
        Returns an empty queryset when request.store is not set — ADR-001 §4:
        cross-store data must never appear on the store-admin surface.
        """
        if getattr(request, "store", None):
            return Collection.objects.for_store(request.store)
        return Collection.objects.none()

    # ------------------------------------------------------------------
    # Save — assign store on creation only
    # ------------------------------------------------------------------

    def save_model(self, request, obj, form, change):
        """
        Assign obj.store = request.store for new collections only.
        Never overwrite the store FK on updates — prevents store-switching attacks.
        """
        if not change and getattr(request, "store", None):
            obj.store = request.store
        super().save_model(request, obj, form, change)

    # ------------------------------------------------------------------
    # Formset save — propagate store FK to inline rows (TICKET-024)
    # ------------------------------------------------------------------

    def save_formset(self, request, form, formset, change):
        """
        Propagate the parent Collection's store to each inline instance that carries
        a store FK but has none set yet.

        Without this override, new CollectionTranslation rows added via the inline
        would hit a NOT NULL constraint on the store column.
        """
        instances = formset.save(commit=False)
        for instance in instances:
            if hasattr(instance, "store_id") and not instance.store_id:
                instance.store = form.instance.store
            instance.save()
        formset.save_m2m()
        for obj_to_delete in formset.deleted_objects:
            obj_to_delete.delete()


# ---------------------------------------------------------------------------
# Super-admin CollectionSuperAdmin — cross-store read
# ---------------------------------------------------------------------------

@admin.register(Collection, site=super_admin_site)
class CollectionSuperAdmin(admin.ModelAdmin):
    list_display = ["title", "store", "collection_type", "is_published", "updated_at"]
    list_filter = ["collection_type", "is_published", "store"]
    search_fields = ["title", "slug"]
    raw_id_fields = ["store"]

    def get_queryset(self, request):
        return Collection.objects.cross_store_unsafe()


# ---------------------------------------------------------------------------
# StockNotificationAdmin — store-admin read-only view
# ---------------------------------------------------------------------------

@admin.register(StockNotification, site=store_admin_site)
class StockNotificationAdmin(admin.ModelAdmin):
    """
    Read-only list of back-in-stock notification requests for the current store.

    Store admins can see who registered for which product and when, and whether
    the notification email has already been sent.
    """

    # ADR-033 D3c DECIDED (human 2026-07-11, ADR-033 D7 item 6): no dedicated row for stock notifications in the
    # 19-module vocabulary; gated as a product-editing concern like the rest
    # of catalog/admin.py. add/change stay hardcoded False below regardless
    # of module grant — this is a customer-submitted signup log, not
    # admin-authored data (mirrors StockNotificationAdmin's pre-ADR-033
    # behavior exactly).
    module_key = "products"

    list_display = ["email", "product", "created_at", "notified_at"]
    list_filter = ["notified_at"]
    search_fields = ["email", "product__title"]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]
    readonly_fields = ["store", "product", "email", "created_at", "notified_at"]

    def has_add_permission(self, request):
        return False

    def has_change_permission(self, request, obj=None):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return StockNotification.objects.for_store(request.store).select_related("product")
        return StockNotification.objects.none()


# ---------------------------------------------------------------------------
# QuotationRequestAdmin — store-admin inbox (ADR-018 D4, T029-SP)
# ---------------------------------------------------------------------------

@admin.register(QuotationRequest, site=store_admin_site)
class QuotationRequestAdmin(admin.ModelAdmin):
    """
    Quotation request inbox for the current store (ADR-018 D4).

    Covers both product-anchored quotations (from `/products/<slug>/quotation/`)
    and general quotations (from the store-level quotation StaticPage, product=NULL)
    in ONE list with one status lifecycle — the "(general)" label makes the NULL
    case an explicit, visible discriminator rather than a blank cell.

    status is editable at "pages" full access (the inbox is a workflow, not a log —
    this fixes the previously fully-read-only admin, which contradicted the model's
    documented pending → responded → closed lifecycle); "limited" access is read-only.
    Module gate uses the "pages" key (ASSUMPTION 18:A2, frozen by ADR-033).
    has_module_permission/has_view_permission/has_change_permission are
    deliberately overridden below (not left to the generic module_key
    mapping) so that "limited" access can still OPEN the read-only change
    form — a genuine app-specific business rule preserved via
    stores.permissions._find_override (ADR-033 D3b docstring).
    """

    module_key = "pages"

    list_display = [
        "customer_email", "customer_name", "product_label", "quantity", "status", "created_at",
    ]
    list_filter = ["status"]
    search_fields = ["customer_email", "customer_name", "product__title"]
    date_hierarchy = "created_at"
    ordering = ["-created_at"]

    def product_label(self, obj):
        return obj.product.title if obj.product_id else "(general)"

    product_label.short_description = "Product"

    def get_readonly_fields(self, request, obj=None):
        """
        status is editable at "pages" full access; everything else stays read-only —
        the inbox records buyer-submitted data that must never be silently rewritten.
        """
        base = ["store", "product", "customer_name", "customer_email", "message", "quantity", "created_at"]
        if check_module_access(request, "pages", "full"):
            return base
        return base + ["status"]

    def has_add_permission(self, request):
        return False

    def has_delete_permission(self, request, obj=None):
        return False

    def has_module_permission(self, request):
        return check_module_access(request, "pages", "limited")

    def has_view_permission(self, request, obj=None):
        return check_module_access(request, "pages", "limited")

    def has_change_permission(self, request, obj=None):
        # "limited" may open the change form (to view details) but all fields are
        # read-only in that case (see get_readonly_fields); "full" may edit status.
        return check_module_access(request, "pages", "limited")

    def get_queryset(self, request):
        if getattr(request, "store", None):
            return QuotationRequest.objects.for_store(request.store).select_related("product")
        return QuotationRequest.objects.none()
