"""
Catalog models: Product, ProductVariant, ProductImage, Collection, CollectionProduct,
VariantOption, VariantOptionValue, VariantOptionAssignment, and all translation models.

Key design decisions (see 09_architecture_decisions.md and 10_implementation_tickets.md):
- inventory_mode lives on ProductVariant, NOT on Product (DECIDED AC-U2, 2026-07-02).
- All models extend StoreOwnedModel (ADR-001 §4).
- Slugs are populated via make_slug() from core.slugs (ADR-005 §1 / §II).
- A pre_save signal (catalog/signals.py) emits slug_changed for any slug-bearing model;
  the redirect table consumer is wired in TICKET-016.
- Video: embed URL only, no file upload (DECIDED AF-C2). YouTube and Vimeo validated
  by validate_video_url() in catalog/validators.py.
- ProductImage lives in the catalog app (not the media app) to keep the product FK
  in one place.
- Collection.smart_rules: list of rule dicts evaluated by catalog/smart_rules.py.
  Price matching uses the store-default-currency base price (v1, PENDING Part B).
- CollectionProduct.sort_key is a stable lexicographic string; deleting a membership
  row never renumbers siblings (TICKET-005, §XV-2).

Phase 1 — Stable variant option entities (TICKET-024):
- VariantOption / VariantOptionValue / VariantOptionAssignment provide stable PKs
  for keying translation records. The legacy option_values_json field on ProductVariant
  is preserved for backward compatibility; the stable entities coexist with it.

Phase 2 — Translation models (TICKET-024, ADR-014):
- ProductImageTranslation, VariantOptionTranslation, VariantOptionValueTranslation follow
  the same pattern as ProductTranslation (ADR-009 App. B).
- Fallback rule (ADR-014 DECIDED 2026-07-05): when no published VariantOption/Value
  translation exists for a language, the template layer shows the source-language label.
  Never 404 on a missing option translation.
- source_fingerprint is SHA-256 of the normalized source label; allows detecting
  source-language changes that require re-translation without reading the row again.
- manually_edited_at is NULL for pure AI-produced content; set by the admin form
  save_model when a human edits a translation. Never set by persist_output.
"""

import hashlib

from django.db import models
from django.db.models import Q, UniqueConstraint
from django.db.models.functions import Upper

from catalog.validators import validate_video_url
from core.models import StoreOwnedModel
from core.slugs import make_slug
from media.validators import validate_image_file


def _fingerprint(text: str) -> str:
    """Return the SHA-256 hex digest of the normalized (stripped, lowercased) text.

    Used by VariantOption and VariantOptionValue to detect source-language changes
    without requiring a separate read-back of the row (ADR-014 §3).
    """
    return hashlib.sha256(text.strip().lower().encode("utf-8")).hexdigest()


def translation_fingerprint(*values: str) -> str:
    """Return SHA-256 hex of the '\\x1f'-joined normalized source-field values.

    Used by persist_output to store source_fingerprint on translation records,
    and by signals to detect whether the source content has changed since the
    last translation run (staleness check, ADR-014 §3 / §Q5).

    Field order for each content type must be fixed and consistent across
    callers. For Product/Collection: (title, description, seo_title, seo_description).
    This function is intentionally order-sensitive — different orderings produce
    different hashes.

    An empty values list or all-empty strings returns a stable hash of ''.
    """
    text = "\x1f".join(v.strip() if v else "" for v in values)
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


class InventoryMode(models.TextChoices):
    SOLD_OUT = 'sold_out', 'Sold out (always)'
    NO_TRACKING = 'no_tracking', 'No inventory tracking'
    FIXED_QTY = 'fixed_qty', 'Track fixed quantity'
    FAKE_SERVER = 'fake_server', 'Server-assigned display (fake scarcity)'
    PRESALE = 'presale', 'Pre-sale (charge on placement)'
    ASK_WHEN_AVAILABLE = 'ask_when_available', 'Ask me when available'
    QUOTATION = 'quotation', 'Ask for quotation'


class Product(StoreOwnedModel):
    """
    A product in the catalog.

    Invariants:
    - slug is auto-populated from make_slug(title) when left blank; unique per store.
    - inventory_mode is on ProductVariant, not here (DECIDED AC-U2).
    - video_url accepts YouTube/Vimeo embed URLs only (DECIDED AF-C2).
    - tags is a list of strings stored as JSON.
    """

    STATUS_DRAFT = 'draft'
    STATUS_ACTIVE = 'active'
    STATUS_ARCHIVED = 'archived'

    _STATUS_CHOICES = [
        (STATUS_DRAFT, 'Draft'),
        (STATUS_ACTIVE, 'Active'),
        (STATUS_ARCHIVED, 'Archived'),
    ]

    title = models.CharField(max_length=512)
    slug = models.SlugField(max_length=512, blank=True)
    description = models.TextField(blank=True, default='')
    status = models.CharField(
        max_length=16,
        choices=_STATUS_CHOICES,
        default=STATUS_DRAFT,
    )
    tags = models.JSONField(default=list, blank=True)
    seo_title = models.CharField(max_length=255, blank=True, default='')
    seo_description = models.TextField(blank=True, default='')
    video_url = models.URLField(
        max_length=500,
        blank=True,
        default='',
        validators=[validate_video_url],
    )
    vendor = models.CharField(max_length=255, blank=True, default='')
    product_type = models.CharField(max_length=100, blank=True, default='')
    # Set automatically via pre_save signal when status transitions to active.
    published_at = models.DateTimeField(null=True, blank=True)
    requires_shipping = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    @classmethod
    def status_choices(cls):
        """Return the list of (value, label) status choices, for use in forms."""
        return cls._STATUS_CHOICES

    def save(self, *args, **kwargs):
        if not self.slug:
            self.slug = make_slug(self.title)
        super().save(*args, **kwargs)

    def __str__(self):
        return self.title

    class Meta:
        verbose_name = 'product'
        verbose_name_plural = 'products'
        unique_together = [('store', 'slug')]
        indexes = [
            models.Index(fields=['store', 'status']),
            models.Index(fields=['store', 'updated_at']),
        ]


class ProductVariant(StoreOwnedModel):
    """
    A variant of a Product (e.g. "Red / Large").

    Invariants:
    - inventory_mode is per-variant (DECIDED AC-U2, 2026-07-02).
    - sku uniqueness per store is enforced case-insensitively in clean() for
      non-empty SKUs only (blank SKUs are allowed for draft products).
      A DB-level UniqueConstraint on (Upper('sku'), store) acts as belt-and-suspenders;
      both the DB constraint and clean() scope uniqueness to the store.
    - quantity is only meaningful when inventory_mode == FIXED_QTY.
    - presale_ships_at is only meaningful when inventory_mode == PRESALE.
    - option_values_json holds structured option values:
        [{"option_name": "Color", "value": "Red"}, {"option_name": "Size", "value": "M"}]
    - is_default flags the variant shown by default on the product page.
    - position controls display ordering within the product (ascending).
    """

    product = models.ForeignKey(
        'catalog.Product',
        on_delete=models.CASCADE,
        related_name='variants',
    )
    sku = models.CharField(max_length=255, blank=True, default='')
    title = models.CharField(max_length=255, default='Default')
    price = models.DecimalField(max_digits=12, decimal_places=2)
    compare_at_price = models.DecimalField(
        max_digits=12, decimal_places=2, null=True, blank=True
    )
    weight_grams = models.PositiveIntegerField(default=0)
    inventory_mode = models.CharField(
        max_length=32,
        choices=InventoryMode.choices,
        default=InventoryMode.NO_TRACKING,
    )
    quantity = models.PositiveIntegerField(null=True, blank=True)
    presale_ships_at = models.DateField(null=True, blank=True)
    is_active = models.BooleanField(default=True)
    # Structured option values: [{"option_name": "Color", "value": "Red"}, ...]
    option_values_json = models.JSONField(default=list)
    # The variant shown by default on the product page.
    is_default = models.BooleanField(default=False)
    # Display ordering within the product (ascending).
    position = models.SmallIntegerField(default=0)
    created_at = models.DateTimeField(auto_now_add=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        # ADR-031 addendum audit (TICKET-051): guard on self.store_id, not just
        # self.sku. Found via the PK-tamper regression test required by the
        # addendum (catalog/tests/test_product_variant_inline_pk_isolation_
        # regression.py): when an inline formset POST supplies a hidden pk
        # that fails lookup against the (now correctly) scoped formset
        # queryset, Django's own BaseModelFormSet._construct_form() falls
        # back to a brand-new, blank ProductVariant() instance for that row
        # (instead of erroring immediately) and still populates it from the
        # row's OTHER submitted fields before running full_clean() — so sku
        # can be truthy on an instance whose store (and product) were never
        # assigned. self.store crashed with RelatedObjectDoesNotExist (an
        # unhandled 500, not a clean ValidationError) before this guard. The
        # row is never actually persisted either way — the pk field's own
        # validation error already blocks the save — the DB-level
        # UniqueConstraint on (Upper('sku'), store) remains the
        # belt-and-suspenders check for any row that IS store-assigned.
        if self.sku and self.store_id:
            qs = ProductVariant.objects.for_store(self.store).filter(sku__iexact=self.sku)
            if self.pk:
                qs = qs.exclude(pk=self.pk)
            if qs.exists():
                raise ValidationError(
                    {'sku': f"A variant with SKU '{self.sku}' already exists in this store (case-insensitive)."}
                )

    def __str__(self):
        return f"{self.product.title} — {self.title}"

    class Meta:
        verbose_name = 'product variant'
        verbose_name_plural = 'product variants'
        indexes = [
            models.Index(fields=['store', 'inventory_mode']),
        ]
        constraints = [
            # Belt-and-suspenders DB-level guard: case-insensitive SKU uniqueness
            # scoped per store. Per-store scoping is also enforced in clean().
            UniqueConstraint(
                Upper('sku'),
                'store',
                name='variant_sku_case_insensitive_per_store',
                condition=~Q(sku=''),
            ),
        ]


class ProductImage(StoreOwnedModel):
    """
    An image attached to a product.

    Lives in the catalog app (not the media app) so the product FK stays in one place.
    Language-agnostic: one binary per image, referenced by product.

    Invariants:
    - display_order controls listing order (ascending).
    - is_primary flags the cover image; callers must ensure at most one per product
      (not enforced at DB level — product may have no primary initially).
    - validate_image_file enforces size and content-type constraints (from media.validators).
    """

    product = models.ForeignKey(
        'catalog.Product',
        on_delete=models.CASCADE,
        related_name='images',
    )
    image = models.ImageField(
        upload_to='products/%Y/%m/',
        validators=[validate_image_file],
    )
    alt_text = models.CharField(max_length=255, blank=True, default='')
    display_order = models.PositiveSmallIntegerField(default=0)
    is_primary = models.BooleanField(default=False)
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"Image for {self.product.title} ({self.display_order})"

    class Meta:
        ordering = ['display_order']
        indexes = [
            models.Index(fields=['product', 'is_primary']),
        ]


class Collection(StoreOwnedModel):
    """
    A product collection — either manually curated or auto-populated by smart rules.

    Invariants:
    - collection_type determines whether memberships are driven by smart_rules (smart)
      or by manual CollectionProduct rows (manual).
    - slug is auto-populated from make_slug(title) when blank; unique per store.
    - smart_rules is a list of rule dicts:
        [{"field": "title|price|tag", "op": "...", "value": "..."}]
      Evaluated by catalog/smart_rules.py. Price uses the store-default-currency base
      price (v1, multi-currency matching PENDING Part B).
    - smart_rules_match ('all'/'any') controls whether ALL or ANY rules must match.
    - Empty auto-collections return 404 on storefront; 1–2-product collections are
      noindex (AC-032, SEO rule consumed by TICKET-025).
    """

    class CollectionType(models.TextChoices):
        SMART = 'smart', 'Smart (auto)'
        MANUAL = 'manual', 'Manual'

    class MatchType(models.TextChoices):
        ALL = 'all', 'All conditions'
        ANY = 'any', 'Any condition'

    title = models.CharField(max_length=512)
    slug = models.SlugField(max_length=512, blank=True)
    description = models.TextField(blank=True, default='')
    collection_type = models.CharField(
        max_length=16,
        choices=CollectionType.choices,
        default=CollectionType.MANUAL,
    )
    is_published = models.BooleanField(default=False)
    seo_title = models.CharField(max_length=255, blank=True, default='')
    seo_description = models.TextField(blank=True, default='')
    image = models.ImageField(
        upload_to='collections/%Y/%m/',
        null=True,
        blank=True,
    )
    smart_rules = models.JSONField(
        default=list,
        help_text=(
            'List of rule dicts: [{"field": "title|price|tag", '
            '"op": "contains|equals|greater_than|less_than|starts_with", "value": "..."}]. '
            'Evaluated by catalog/smart_rules.py. Price uses base price only (v1).'
        ),
    )
    smart_rules_match = models.CharField(
        max_length=8,
        choices=MatchType.choices,
        default=MatchType.ALL,
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def save(self, *args, **kwargs):
        if not self.slug:
            self.slug = make_slug(self.title)
        super().save(*args, **kwargs)

    def __str__(self):
        return self.title

    class Meta:
        verbose_name = 'collection'
        verbose_name_plural = 'collections'
        unique_together = [('store', 'slug')]
        indexes = [
            models.Index(fields=['store', 'is_published']),
        ]


class TranslationStatus(models.TextChoices):
    DRAFT = "draft", "Draft"
    PUBLISHED = "published", "Published"


class ProductTranslation(StoreOwnedModel):
    """
    Translated text fields for a Product in a specific language (TICKET-024, ADR-005 §2).

    Translated slugs do NOT live here — they live in Permalink (ADR-005 §2).
    This model holds translated title, description, and SEO metadata only.

    When status transitions to PUBLISHED, a Permalink is auto-created or activated
    via post_save signal (catalog/signals.py). When reverted to DRAFT, the active
    Permalink for this (store, lang, product) is deactivated, causing a 404 on the
    translated URL (AC-104).

    ai_job links the AiJob that produced this translation, if any.
    """

    product = models.ForeignKey(
        Product,
        on_delete=models.CASCADE,
        related_name="translations",
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    title = models.CharField(max_length=255, blank=True)
    description = models.TextField(blank=True)
    seo_title = models.CharField(max_length=255, blank=True)
    seo_description = models.CharField(max_length=500, blank=True)
    status = models.CharField(
        max_length=20,
        choices=TranslationStatus.choices,
        default=TranslationStatus.DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="product_translations",
    )
    # NULL = pure AI content; set by admin save_model when a human edits the
    # translation. Never set by persist_output (ADR-014 §4).
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    # SHA-256 hex of the '\x1f'-joined source fields (title, description, seo_title,
    # seo_description) at translation time. Written by persist_output; empty for rows
    # created before fingerprinting was added (treated as stale on next source save).
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    # Suggested slug written by a slug_only AiJob (ADR-014 §Q6, T024).
    # Used as the initial slug for the auto-created Permalink when transitioning to
    # PUBLISHED.  When non-empty and the Permalink is auto_created=True, the permalink
    # signal also updates the existing Permalink slug.
    # Overridden when a human manually edits the Permalink slug directly (not here).
    # Never set by product/full translation jobs — only by slug_only jobs.
    slug_hint = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text=(
            "Suggested slug from AI slug_only job. Used as the initial slug for the "
            "auto-created Permalink; updated automatically when the signal fires."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.store_id and self.product_id and self.store_id != self.product.store_id:
            raise ValidationError(
                "Translation store must match the product's store."
            )

    def __str__(self):
        return f"{self.product.title} [{self.lang_code}]"

    class Meta:
        verbose_name = "product translation"
        verbose_name_plural = "product translations"
        unique_together = [("store", "product", "lang_code")]


class CollectionTranslation(StoreOwnedModel):
    """
    Translated text fields for a Collection in a specific language (TICKET-024, ADR-005 §2).

    Follows the same lifecycle as ProductTranslation: translated slugs live in
    Permalink, not here. Publishing activates the Permalink; reverting to DRAFT
    deactivates it (404 on translated URL, AC-104).
    """

    collection = models.ForeignKey(
        Collection,
        on_delete=models.CASCADE,
        related_name="translations",
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    title = models.CharField(max_length=255, blank=True)
    description = models.TextField(blank=True)
    seo_title = models.CharField(max_length=255, blank=True)
    seo_description = models.CharField(max_length=500, blank=True)
    status = models.CharField(
        max_length=20,
        choices=TranslationStatus.choices,
        default=TranslationStatus.DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="collection_translations",
    )
    # NULL = pure AI content; set by admin save_model when a human edits the
    # translation. Never set by persist_output (ADR-014 §4).
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    # SHA-256 hex of the '\x1f'-joined source fields at translation time (ADR-014 §3).
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.store_id and self.collection_id and self.store_id != self.collection.store_id:
            raise ValidationError(
                "Translation store must match the collection's store."
            )

    def __str__(self):
        return f"{self.collection.title} [{self.lang_code}]"

    class Meta:
        verbose_name = "collection translation"
        verbose_name_plural = "collection translations"
        unique_together = [("store", "collection", "lang_code")]


class CollectionProduct(StoreOwnedModel):
    """
    Through-model linking a Collection to a Product.

    Invariants:
    - sort_key is a stable lexicographic string used for ordering.
      Deleting a row NEVER renumbers siblings (§XV-2 / TICKET-005).
      Callers must assign meaningful sort_key values (e.g. fractional indexing strings)
      when manual ordering is required; the default empty string is valid for smart
      collections where insertion order is acceptable.
    - The combination (collection, product) is unique.
    - store must match collection.store and product.store; enforced by application logic.
    """

    collection = models.ForeignKey(
        'catalog.Collection',
        on_delete=models.CASCADE,
        related_name='memberships',
    )
    product = models.ForeignKey(
        'catalog.Product',
        on_delete=models.CASCADE,
        related_name='collection_memberships',
    )
    sort_key = models.CharField(
        max_length=255,
        default='',
        help_text=(
            'Stable lexicographic ordering key. Never renumbered on sibling removal. '
            'Use fractional indexing strings for manual drag-ordering.'
        ),
    )
    added_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"{self.product.title} in {self.collection.title}"

    class Meta:
        verbose_name = 'collection product'
        verbose_name_plural = 'collection products'
        ordering = ['sort_key']
        unique_together = [('collection', 'product')]


# ---------------------------------------------------------------------------
# Phase 1 — Stable variant option entities (TICKET-024)
# ---------------------------------------------------------------------------

class VariantOption(StoreOwnedModel):
    """
    A named dimension along which a product varies (e.g. "Color", "Size").

    Stable-PK entity that replaces the freeform JSON strings previously stored
    in ProductVariant.option_values_json.  The JSON field is preserved for
    backward compatibility; these models coexist with it.

    source_fingerprint — SHA-256 of normalized option name (stripped + lowercased).
        Set automatically on save.  Allows detecting source-language content changes
        that require re-translation without reading the row again (ADR-014 §3).
    manually_edited_at — NULL = pure data import / AI content; set when a human
        edits the name via the admin.  Never set by automated import code.
    position — ascending display order within the product's option list.
    """

    product = models.ForeignKey(
        'catalog.Product',
        on_delete=models.CASCADE,
        related_name='variant_options',
    )
    name = models.CharField(
        max_length=100,
        help_text="Option dimension name, e.g. 'Color', 'Size'.",
    )
    position = models.PositiveSmallIntegerField(
        default=0,
        help_text="Display order within the product's option list (ascending).",
    )
    source_fingerprint = models.CharField(
        max_length=64,
        blank=True,
        help_text="SHA-256 hex digest of the normalized (stripped, lowercased) name.",
    )
    manually_edited_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when a human edits this option via admin. NULL = not manually edited.",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def save(self, *args, **kwargs):
        self.source_fingerprint = _fingerprint(self.name)
        super().save(*args, **kwargs)

    def __str__(self):
        return f"{self.name} (product={self.product_id})"

    class Meta:
        verbose_name = 'variant option'
        verbose_name_plural = 'variant options'
        unique_together = [('store', 'product', 'name')]
        ordering = ['position', 'pk']


class VariantOptionValue(StoreOwnedModel):
    """
    One possible value for a VariantOption (e.g. "Red" for the "Color" option).

    source_fingerprint — SHA-256 of normalized value string; same purpose as on
        VariantOption (ADR-014 §3).
    manually_edited_at — NULL = not manually edited.
    position — ascending display order within the option.
    """

    option = models.ForeignKey(
        'catalog.VariantOption',
        on_delete=models.CASCADE,
        related_name='values',
    )
    value = models.CharField(
        max_length=200,
        help_text="Option value label, e.g. 'Red', 'XL'.",
    )
    position = models.PositiveSmallIntegerField(
        default=0,
        help_text="Display order within the option (ascending).",
    )
    source_fingerprint = models.CharField(
        max_length=64,
        blank=True,
        help_text="SHA-256 hex digest of the normalized (stripped, lowercased) value.",
    )
    manually_edited_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when a human edits this value via admin. NULL = not manually edited.",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def save(self, *args, **kwargs):
        self.source_fingerprint = _fingerprint(self.value)
        super().save(*args, **kwargs)

    def __str__(self):
        return f"{self.value} ({self.option.name})"

    class Meta:
        verbose_name = 'variant option value'
        verbose_name_plural = 'variant option values'
        unique_together = [('store', 'option', 'value')]
        ordering = ['position', 'pk']


class VariantOptionAssignment(StoreOwnedModel):
    """
    Links one ProductVariant to a specific VariantOption + VariantOptionValue pair.

    Together, the assignments for a variant fully describe its selected option
    values (e.g. "Color=Red, Size=XL"), replacing the freeform JSON in
    ProductVariant.option_values_json.

    Invariants:
    - (store, variant, option) is unique — a variant has at most one value per option.
    - The value FK must belong to the option FK; this is not enforced at DB level
      but is enforced by application code on creation.
    """

    variant = models.ForeignKey(
        'catalog.ProductVariant',
        on_delete=models.CASCADE,
        related_name='option_assignments',
    )
    option = models.ForeignKey(
        'catalog.VariantOption',
        on_delete=models.CASCADE,
        related_name='+',
    )
    value = models.ForeignKey(
        'catalog.VariantOptionValue',
        on_delete=models.CASCADE,
        related_name='+',
    )

    def __str__(self):
        return f"Variant {self.variant_id}: {self.option.name}={self.value.value}"

    class Meta:
        verbose_name = 'variant option assignment'
        verbose_name_plural = 'variant option assignments'
        unique_together = [('store', 'variant', 'option')]


# ---------------------------------------------------------------------------
# Phase 2 — Additional translation models (TICKET-024, ADR-014)
# ---------------------------------------------------------------------------

class ProductImageTranslation(StoreOwnedModel):
    """
    Translated alt text for a ProductImage (TICKET-024, ADR-014).

    One row per (store, image, lang_code). Status lifecycle: draft → published.
    Published alt text is served on the translated storefront; draft is suppressed.

    alt_text has no URL/Permalink — publishing this translation does NOT trigger the
    Permalink lifecycle (that lifecycle is for Product/Collection only).

    manually_edited_at — NULL = pure AI content; set by admin save_model on human edit.
    """

    image = models.ForeignKey(
        'catalog.ProductImage',
        on_delete=models.CASCADE,
        related_name='translations',
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    alt_text = models.CharField(max_length=300, blank=True)
    status = models.CharField(
        max_length=20,
        choices=TranslationStatus.choices,
        default=TranslationStatus.DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="product_image_translations",
    )
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.store_id and self.image_id and self.store_id != self.image.store_id:
            raise ValidationError(
                "Translation store must match the image's store."
            )

    def __str__(self):
        return f"ImageTranslation image={self.image_id} [{self.lang_code}]"

    class Meta:
        verbose_name = "product image translation"
        verbose_name_plural = "product image translations"
        unique_together = [("store", "image", "lang_code")]


class VariantOptionTranslation(StoreOwnedModel):
    """
    Translated name for a VariantOption (TICKET-024, ADR-014).

    Fallback rule (ADR-014 DECIDED 2026-07-05): when no PUBLISHED translation
    exists for a given language, the template/view layer falls back to the
    source-language label (VariantOption.name). Never 404 on a missing option
    translation. This fallback is implemented at the template/view layer, not here.

    manually_edited_at — NULL = pure AI content; set by admin save_model on human edit.
    """

    option = models.ForeignKey(
        'catalog.VariantOption',
        on_delete=models.CASCADE,
        related_name='translations',
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    name = models.CharField(
        max_length=100,
        blank=True,
        help_text="Translated option name. Falls back to VariantOption.name when blank/unpublished.",
    )
    status = models.CharField(
        max_length=20,
        choices=TranslationStatus.choices,
        default=TranslationStatus.DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="variant_option_translations",
    )
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.store_id and self.option_id and self.store_id != self.option.store_id:
            raise ValidationError(
                "Translation store must match the option's store."
            )

    def __str__(self):
        return f"OptionTranslation {self.option.name!r} [{self.lang_code}]"

    class Meta:
        verbose_name = "variant option translation"
        verbose_name_plural = "variant option translations"
        unique_together = [("store", "option", "lang_code")]


# ---------------------------------------------------------------------------
# Storefront engagement models (T029 TH-082)
# ---------------------------------------------------------------------------

class StockNotification(StoreOwnedModel):
    """
    Records a customer's request to be notified when a product is back in stock.

    Invariants:
    - (store, product, email) is unique — registering the same email twice is
      silently ignored (idempotent POST handler in storefront/views_product_forms.py).
    - notified_at is set externally by the back-in-stock notification job when
      the notification email has been sent; NULL means not yet notified.
    """

    product = models.ForeignKey(
        "catalog.Product",
        on_delete=models.CASCADE,
        related_name="stock_notifications",
    )
    email = models.EmailField()
    created_at = models.DateTimeField(auto_now_add=True)
    notified_at = models.DateTimeField(null=True, blank=True)

    def __str__(self):
        return f"StockNotification({self.email}, product={self.product_id})"

    class Meta:
        verbose_name = "stock notification"
        verbose_name_plural = "stock notifications"
        unique_together = [("store", "product", "email")]
        indexes = [
            # NOTE (T029-SP drift fix): migration 0010 named this index
            # 'catalog_stkn_store_notified_idx' (31 chars) — invalid per Django's
            # own index-name-length check (models.E034, max 30 chars; this was never
            # caught before because Meta.indexes had no explicit `name=`, so Django
            # silently used its own valid auto-generated hash name at check time
            # while the DB carried the too-long name from the migration). Using a
            # corrected, valid name here is intentional and requires one rename
            # migration — matching the invalid original name is not an option, it
            # fails manage.py check (E034).
            models.Index(
                fields=["store", "notified_at"],
                name="catalog_stkn_store_ntfy_idx",
            ),
        ]


class QuotationRequest(StoreOwnedModel):
    """
    Records a customer's request for a price quotation on a quote-only product.

    Invariants:
    - status lifecycle: pending → responded → closed.
    - quantity is always >= 1.
    - Multiple requests from the same customer for the same product are allowed
      (no unique_together constraint — customers may request quotes at different
      quantities or times).
    - product is nullable (ADR-018 D4, T029-SP): NULL means a general (non-product)
      quotation submitted from the store-level quotation StaticPage.  NULL is an
      explicit, queryable discriminator, not state inferred from absence — the row's
      meaning is complete either way.  on_delete=SET_NULL so deleting a product keeps
      the sales lead in the inbox.
    """

    STATUS_PENDING = "pending"
    STATUS_RESPONDED = "responded"
    STATUS_CLOSED = "closed"

    _STATUS_CHOICES = [
        (STATUS_PENDING, "Pending"),
        (STATUS_RESPONDED, "Responded"),
        (STATUS_CLOSED, "Closed"),
    ]

    product = models.ForeignKey(
        "catalog.Product",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="quotation_requests",
        help_text="NULL = general (non-product) quotation request (ADR-018 D4).",
    )
    customer_name = models.CharField(max_length=200, blank=True, default="")
    customer_email = models.EmailField()
    message = models.TextField(blank=True, default="")
    quantity = models.PositiveIntegerField(default=1)
    status = models.CharField(
        max_length=20,
        choices=_STATUS_CHOICES,
        default=STATUS_PENDING,
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        product_label = self.product_id if self.product_id else "(general)"
        return f"QuotationRequest({self.customer_email}, product={product_label})"

    class Meta:
        verbose_name = "quotation request"
        verbose_name_plural = "quotation requests"
        indexes = [
            # Explicit names match migration 0010's auto-generated names so that
            # no rename migration is produced by makemigrations (drift fix, T029-SP).
            models.Index(fields=["store", "status"], name="catalog_qreq_store_status_idx"),
            models.Index(fields=["store", "created_at"], name="catalog_qreq_store_created_idx"),
        ]


# ---------------------------------------------------------------------------
# A/B page versions (TICKET-042, ADR-028)
# ---------------------------------------------------------------------------
#
# NAMING: "variant" already means SKU variant (ProductVariant) in this codebase.
# The word "variant" is BANNED from every identifier in this feature — the
# entity is ProductPageVersion, never "ProductVariant2" / "PageVariant" / etc.
# (ADR-028 "Naming — the collision hazard").


class ProductPageVersion(StoreOwnedModel):
    """
    An alternate URL for a product page that swaps only the image set
    (TICKET-042, ADR-028).

    Same product, same price, same SKU variants, same translated text — only
    the images differ.  Used for Pinterest/ad A/B testing: the merchant drives
    traffic to each version's own URL (link-level split, no automatic
    assignment).

    Invariants:
    - unique_together (product, slug_suffix) — matches ADR-028 §1 exactly.
    - slug_suffix is normalized through core.slugs.make_slug() on every save
      (design-pattern-ideas §II — the ONE slug function).
    - Cap: MAX_VERSIONS_PER_PRODUCT (10) versions per product — a permalink-bloat
      guard, enforced in clean() (ASSUMPTION P1, LOW, human-approved 2026-07-11).
    - Activation (is_active=True) requires >= 1 image in the version's image set
      (ASSUMPTION P3, LOW) — otherwise the page would render identically to the
      primary, a meaningless test and a pure duplicate page.  Enforced by the
      admin (catalog/admin.py ProductPageVersionAdmin.save_related), not here,
      because images are attached via a separate inline after this row exists.
    - The reserved-slug / ISO-639 edge (e.g. product slug 'go' + suffix 'en' =
      'go-en') AND plain composed-slug collisions (the composed slug already
      claimed by any other page in that language) are both rejected by
      _composed_slug_conflicts(), called from clean() (admin-form boundary —
      normal field error) and from save() (every direct/programmatic
      producer — spec review PV-SLUG-COLLISION-500) against every language the
      product has an active permalink in, reusing
      permalinks.models._validate_slug_not_reserved (the ONE validator every
      permalink producer goes through) so a store admin sees a normal form
      error, and a script/AI-persist .save() raises ValidationError, instead of
      Permalink.save() raising IntegrityError deep inside a signal
      (ADR-028 §2 "Edge case").
    - hreflang_exempt is always True (CAN-020): version URLs emit zero hreflang.
      Consumed by storefront/base.html, NOT by changing the hreflang_tags tag
      itself (ADR-028 §4 — "no template-tag change").
    - name is an internal admin label only — never rendered to shoppers, so it
      is not translatable (ADR-028 §1).
    - Permalink lifecycle (create/reactivate/rename/deactivate/delete) is
      signal-driven from catalog/signals.py, mirroring the Product/Collection
      pattern, and implemented in permalinks/registry.py (ADR-028 §2).
    """

    MAX_VERSIONS_PER_PRODUCT = 10

    product = models.ForeignKey(
        'catalog.Product',
        on_delete=models.CASCADE,
        related_name='page_versions',
    )
    name = models.CharField(
        max_length=255,
        help_text="Internal admin label (e.g. 'Model 2 photos'). Never rendered to shoppers.",
    )
    slug_suffix = models.CharField(
        max_length=64,
        help_text="Appended to the product slug: <product-slug>-<suffix>. Normalized via make_slug().",
    )
    is_active = models.BooleanField(
        default=True,
        help_text="Inactive versions 302-redirect their URL(s) to the primary product page.",
    )
    position = models.SmallIntegerField(default=0, help_text="Admin display ordering (ascending).")
    created_at = models.DateTimeField(auto_now_add=True)

    @property
    def hreflang_exempt(self) -> bool:
        """Always True (CAN-020) — version URLs never emit hreflang alternates."""
        return True

    def save(self, *args, **kwargs):
        if self.slug_suffix:
            self.slug_suffix = make_slug(self.slug_suffix)

        # Spec review (wave 3, PV-SLUG-COLLISION-500): the reserved-pattern /
        # composed-slug-availability check below is normally reached via
        # full_clean() (ModelForm.is_valid() calls it — see clean()), but a
        # programmatic save (script, AI persist path, shell) can call .save()
        # directly and skip full_clean() entirely. Without this guard, such a
        # save would insert an orphaned ProductPageVersion row, then the
        # post_save signal (catalog/signals.py -> permalinks/registry.py
        # sync_page_version_permalinks) would raise IntegrityError deep inside
        # Permalink.save() on the (store, lang, slug) unique constraint,
        # leaving the ProductPageVersion committed with no matching Permalink.
        # Same F1-audit precedent as pages/models.py StaticPage.save() calling
        # _validate_slug_not_reserved(self.slug) directly before super().save().
        if self.product_id:
            conflicts = self._composed_slug_conflicts(self.product.store)
            if conflicts:
                from django.core.exceptions import ValidationError
                raise ValidationError({'slug_suffix': conflicts[0]})

        super().save(*args, **kwargs)

    def _composed_slug_conflicts(self, store):
        """
        Check the ADR-028 §2 edge case for every language `store`'s product
        currently has an active permalink in: for each such language, compose
        f'{product_permalink.slug}-{normalized_suffix}' and reject it if either

        (a) it matches the reserved ISO-639 / fixed-route pattern (reuses the
            SAME validator Permalink.save() runs on every write, §XV-4), or
        (b) it is already claimed by another active Permalink in that
            (store, lang) pair — the actual `unique_together = (store, lang,
            slug)` collision that used to escape as an IntegrityError from
            permalinks/registry.py sync_page_version_permalinks() deep inside
            the post_save signal (spec review, PV-SLUG-COLLISION-500). This is
            a real cross-content-type collision check (a version's composed
            slug can collide with a StaticPage, a Collection, another
            product's permalink, or another page version) — not merely the
            reserved-pattern edge case (a) — because Permalink's uniqueness is
            store+lang+slug regardless of content_type.

        Shared by clean() (admin-form boundary — turns the first conflict into
        a field-keyed ValidationError) and save() (every direct/programmatic
        producer — see save() above) so the two enforcement points can never
        drift apart.

        Returns a list of human-readable conflict messages (empty when clear).
        """
        from django.contrib.contenttypes.models import ContentType
        from django.core.exceptions import ValidationError as DjangoValidationError

        from permalinks.models import Permalink, _validate_slug_not_reserved

        if not self.slug_suffix:
            return []

        normalized_suffix = make_slug(self.slug_suffix)
        product_ct = ContentType.objects.get_for_model(Product)
        version_ct = ContentType.objects.get_for_model(ProductPageVersion)
        product_permalinks = Permalink.objects.for_store(store).filter(
            content_type=product_ct, object_id=self.product_id, is_active=True,
        )

        conflicts = []
        for pp in product_permalinks:
            composed = f"{pp.slug}-{normalized_suffix}"

            try:
                _validate_slug_not_reserved(composed)
            except DjangoValidationError as exc:
                conflicts.append(
                    f"The composed URL {composed!r} (language {pp.lang!r}) is reserved: "
                    f"{exc.messages[0]}"
                )
                continue

            # Exclude this SAME version's own permalink for this language — that
            # is a rename-to-same-value no-op, not a collision with another page.
            in_use = (
                Permalink.objects.for_store(store)
                .filter(lang=pp.lang, slug=composed, is_active=True)
                .exclude(content_type=version_ct, object_id=self.pk)
                .exists()
            )
            if in_use:
                conflicts.append(
                    f"The composed URL {composed!r} (language {pp.lang!r}) is already in "
                    "use by another page. Choose a different suffix."
                )

        return conflicts

    def clean(self):
        from django.core.exceptions import ValidationError

        errors = {}

        if self.product_id:
            store = self.product.store

            # Cap: max MAX_VERSIONS_PER_PRODUCT rows per product (P1, ASSUMPTION LOW).
            qs = ProductPageVersion.objects.for_store(store).filter(product_id=self.product_id)
            if self.pk:
                qs = qs.exclude(pk=self.pk)
            if qs.count() >= self.MAX_VERSIONS_PER_PRODUCT:
                errors['slug_suffix'] = (
                    f"A product may have at most {self.MAX_VERSIONS_PER_PRODUCT} page versions."
                )

            # Reserved-slug / ISO-639 composition check AND composed-slug
            # availability check (ADR-028 §2 edge case; spec review
            # PV-SLUG-COLLISION-500) — see _composed_slug_conflicts() above.
            if self.slug_suffix and 'slug_suffix' not in errors:
                conflicts = self._composed_slug_conflicts(store)
                if conflicts:
                    errors['slug_suffix'] = conflicts[0]

        if errors:
            raise ValidationError(errors)

    def __str__(self):
        return f"{self.product.title} — {self.name} ({self.slug_suffix})"

    class Meta:
        verbose_name = 'product page version'
        verbose_name_plural = 'product page versions'
        unique_together = [('product', 'slug_suffix')]
        ordering = ['position', 'pk']


class ProductPageVersionImage(StoreOwnedModel):
    """
    One image in a ProductPageVersion's ordered image set (TICKET-042, ADR-028 §1).

    The image set is an ORDERED SUBSET of the product's existing ProductImage
    rows — no second binary store, no per-version alt text (alt text stays on
    ProductImage / ProductImageTranslation).

    Invariants:
    - unique_together (page_version, image) — an image cannot appear twice in
      the same version's set.
    - image must belong to the SAME product as page_version.product — enforced
      in clean() (not at DB level, matching the codebase's established pattern
      for cross-FK product-consistency checks, e.g. ProductTranslation.clean()).
    - display_order controls rendering order (ascending), mirroring ProductImage.
    """

    page_version = models.ForeignKey(
        'catalog.ProductPageVersion',
        on_delete=models.CASCADE,
        related_name='images',
    )
    image = models.ForeignKey(
        'catalog.ProductImage',
        on_delete=models.CASCADE,
        related_name='page_version_uses',
    )
    display_order = models.PositiveSmallIntegerField(default=0)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.page_version_id and self.image_id:
            if self.image.product_id != self.page_version.product_id:
                raise ValidationError(
                    {'image': "The image must belong to the same product as the page version."}
                )

    def __str__(self):
        return f"Image {self.image_id} in page version {self.page_version_id}"

    class Meta:
        verbose_name = 'product page version image'
        verbose_name_plural = 'product page version images'
        unique_together = [('page_version', 'image')]
        ordering = ['display_order']


class VariantOptionValueTranslation(StoreOwnedModel):
    """
    Translated value label for a VariantOptionValue (TICKET-024, ADR-014).

    Fallback rule (ADR-014 DECIDED 2026-07-05): when no PUBLISHED translation
    exists for a given language, the template/view layer falls back to the
    source-language label (VariantOptionValue.value). Never 404 on a missing
    option value translation. This fallback is implemented at the template/view
    layer, not here.

    manually_edited_at — NULL = pure AI content; set by admin save_model on human edit.
    """

    option_value = models.ForeignKey(
        'catalog.VariantOptionValue',
        on_delete=models.CASCADE,
        related_name='translations',
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    value = models.CharField(
        max_length=200,
        blank=True,
        help_text="Translated value label. Falls back to VariantOptionValue.value when blank/unpublished.",
    )
    status = models.CharField(
        max_length=20,
        choices=TranslationStatus.choices,
        default=TranslationStatus.DRAFT,
    )
    ai_job = models.ForeignKey(
        "aijobs.AiJob",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="variant_option_value_translations",
    )
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if (
            self.store_id
            and self.option_value_id
            and self.store_id != self.option_value.option.store_id
        ):
            raise ValidationError(
                "Translation store must match the option value's store."
            )

    def __str__(self):
        return f"OptionValueTranslation {self.option_value.value!r} [{self.lang_code}]"

    class Meta:
        verbose_name = "variant option value translation"
        verbose_name_plural = "variant option value translations"
        unique_together = [("store", "option_value", "lang_code")]
