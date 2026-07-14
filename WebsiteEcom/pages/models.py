"""
Static page + contact/quotation form models (ADR-018, T029-SP).

StaticPage / StaticPageTranslation:
    Byte-for-byte the catalog.Product / catalog.ProductTranslation pattern (ADR-018 D1) —
    same lifecycle, same translation fields, same Permalink wiring via pages/signals.py.
    Fingerprint field order (translation_fingerprint, imported from catalog.models — do
    NOT duplicate the function): (title, body, seo_title, seo_description). This order is
    fixed and must stay consistent between pages/signals.py and any AI persist path
    (catalog/ai_jobs.py sub_type='static_page').

    hreflang_exempt (ADR-018 D1 SEO, §XI legal no-op): policy pages (kind=POLICY) get
    zero hreflang alternates — both the {% seo_head %} tag and the sitemap builder read
    this ONE property so they can never disagree (CAN-005).

ContactMessage:
    Store-level contact inbox (ADR-018 D3). Deliberately separate from QuotationRequest —
    different lifecycle vocabulary (new/replied/closed vs pending/responded/closed) and no
    quantity/product semantics. `page` is provenance-only (which StaticPage the visitor
    submitted from); SET_NULL so deleting the contact page keeps the message history.

TranslationStatus is imported from catalog.models — the enum is shared platform-wide,
never duplicated (design-pattern-ideas.txt: single enum, not a re-declared string set).
"""

from django.db import models

from catalog.models import TranslationStatus, translation_fingerprint
from core.models import StoreOwnedModel
from core.slugs import make_slug

# Shared with pages/seeding.py (SEO review M2): the literal marker seeded into every
# draft StaticPage body. StaticPage.clean() rejects publishing while this marker is
# still present in body, so an unedited seed can never become an indexable, sitemapped
# page of thin placeholder content. Defined here (not in seeding.py) because models.py
# has no dependency on seeding.py, while seeding.py already imports from models.py —
# importing the other way would create a circular import.
PLACEHOLDER_MARKER = "Replace this text before publishing."


class StaticPageKind(models.TextChoices):
    GENERIC = "generic", "Generic content page"
    CONTACT = "contact", "Contact form page"
    QUOTATION = "quotation", "Quotation form page"
    POLICY = "policy", "Legal / policy page"


# SEO review L4: the single source of truth for which StaticPage kinds are exempt
# from hreflang (§XI legal no-op, CAN-005). Both consumers import THIS constant
# instead of re-expressing "kind == POLICY" independently, so a future exempt kind
# can never be added to one consumer and forgotten in the other:
#   - StaticPage.hreflang_exempt (below) — read by {% hreflang_tags %} / {% seo_head %}.
#   - sitemaps/sitemaps.py PermalinkSitemap.get_urls — the queryset filter building
#     hreflang_exempt_keys (a property can't be queried directly, so the sitemap
#     builder filters StaticPage.kind__in=HREFLANG_EXEMPT_KINDS instead).
HREFLANG_EXEMPT_KINDS = frozenset({StaticPageKind.POLICY})


class StaticPage(StoreOwnedModel):
    """
    A store-owned static content page (About, Contact, Terms, ...) — ADR-018 D1.

    Invariants:
    - slug is auto-populated from make_slug(title) when left blank; unique per store.
    - Root-level slug (no 'pages/' prefix) — same namespace as products; enforced against
      RESERVED_TOP_LEVEL_SLUGS at THREE points (security audit F1): StaticPage.clean()
      (admin-form boundary), StaticPage.save() (every direct-save producer), and
      Permalink.save() (the write-path every permalink producer funnels through) —
      all three call the SAME permalinks.models._validate_slug_not_reserved.
    - body is raw HTML rendered with |safe (ADR-018 D2 — matches product.description).
    - is_published=False pages have no Permalink (drafts are never crawlable).
    - show_in_header / show_in_footer + nav_position drive {% static_page_links %}
      (ADR-018 D6) — no separate menu model exists yet.
    - hreflang_exempt derives from kind — the single source SEO consumers must read.
    """

    title = models.CharField(max_length=512)
    slug = models.SlugField(max_length=512, blank=True)
    body = models.TextField(blank=True, default="")
    kind = models.CharField(
        max_length=16,
        choices=StaticPageKind.choices,
        default=StaticPageKind.GENERIC,
    )
    is_published = models.BooleanField(default=False)
    seo_title = models.CharField(max_length=255, blank=True, default="")
    seo_description = models.TextField(blank=True, default="")
    show_in_header = models.BooleanField(default=False)
    show_in_footer = models.BooleanField(default=False)
    nav_position = models.SmallIntegerField(
        default=0,
        help_text="Ascending display order within each nav zone (header/footer).",
    )
    published_at = models.DateTimeField(null=True, blank=True)
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    @property
    def hreflang_exempt(self) -> bool:
        """
        True for legal/policy pages (§XI legal no-op) — {% seo_head %} and the sitemap
        builder both read this single property (or, on the sitemap side, filter by the
        same HREFLANG_EXEMPT_KINDS constant this property reads) so page-level hreflang
        and sitemap alternates can never disagree (CAN-005).
        """
        return self.kind in HREFLANG_EXEMPT_KINDS

    def clean(self):
        """
        Reject publishing while the seeded placeholder marker is still present in
        body (SEO review M2): an unedited seed publish would otherwise create an
        indexable, sitemapped page whose entire content is placeholder text —
        thin content, not real legal boilerplate. Editing the body (removing the
        marker) before publishing always succeeds.

        Also rejects reserved top-level slugs / ISO-639 language codes (security
        audit F1) at the admin-form boundary — full_clean() is what a ModelForm
        (StaticPageAdminForm) calls, so a store admin gets a normal field error
        instead of the page silently publishing with no reachable URL. Reuses
        permalinks.models._validate_slug_not_reserved (imported, not duplicated)
        so the reserved set can never drift between this check and the one
        Permalink.save() enforces on every write (§XV-4).
        """
        from django.core.exceptions import ValidationError

        from permalinks.models import _validate_slug_not_reserved

        if self.slug:
            try:
                _validate_slug_not_reserved(self.slug)
            except ValidationError as exc:
                raise ValidationError({"slug": exc.messages}) from exc

        if self.is_published and PLACEHOLDER_MARKER in (self.body or ""):
            raise ValidationError({
                "body": (
                    "This page still contains the seeded placeholder text "
                    f"({PLACEHOLDER_MARKER!r}). Replace it with real content "
                    "before publishing."
                )
            })

    def _skip_redirect_if_no_active_permalink(self):
        """
        Set the _skip_redirect escape hatch (SEO review L5) when the slug is
        changing but the OLD slug has no active Permalink to redirect from —
        e.g. a draft that was renamed before ever being published. Without this,
        permalinks/signals.py handle_slug_change would still write a live
        SlugRedirect whose target the resolver 404s on (nothing was ever
        indexed at the old slug, so there is nothing worth redirecting).

        Reuses the SAME `_skip_redirect` flag ProductAdmin.save_model already
        sets before calling obj.save() (permalinks/signals.py honours it) —
        no change to permalinks/signals.py is needed.
        """
        from django.contrib.contenttypes.models import ContentType

        from permalinks.models import Permalink

        try:
            old = StaticPage.objects.cross_store_unsafe().get(pk=self.pk)
        except StaticPage.DoesNotExist:
            return
        if old.slug == self.slug:
            return  # slug is not changing — nothing to guard against

        ct = ContentType.objects.get_for_model(StaticPage)
        has_active_permalink = (
            Permalink.objects.cross_store_unsafe()
            .filter(
                store=self.store, content_type=ct, object_id=self.pk,
                lang=self.store.primary_language, slug=old.slug, is_active=True,
            )
            .exists()
        )
        if not has_active_permalink:
            self._skip_redirect = True

    def save(self, *args, **kwargs):
        """
        Security audit F1: reject a reserved top-level slug / ISO-639 language code
        BEFORE this row (or any Permalink) is written — not only via clean(), which
        only runs when a caller explicitly calls full_clean() (ModelForm does; a
        direct StaticPage(...).save() from a script, test, or the AI persist path
        does not). Without this, the audit's own repro
        (StaticPage(slug='cart', is_published=True).save()) would insert the
        StaticPage row before pages/signals.py's post_save handler ever got a
        chance to reject the Permalink, leaving an inconsistent published-but-
        unreachable page. Checking here means .save() raises first — no row at
        all is written, for both the page and its permalink.
        """
        if not self.slug:
            self.slug = make_slug(self.title)

        from permalinks.models import _validate_slug_not_reserved

        _validate_slug_not_reserved(self.slug)

        if self.pk:
            self._skip_redirect_if_no_active_permalink()
        super().save(*args, **kwargs)

    def __str__(self):
        return self.title

    class Meta:
        verbose_name = "static page"
        verbose_name_plural = "static pages"
        unique_together = [("store", "slug")]
        indexes = [
            models.Index(fields=["store", "is_published"]),
        ]


class StaticPageTranslation(StoreOwnedModel):
    """
    Translated text fields for a StaticPage in a specific language (ADR-018 D1).

    Byte-for-byte the ProductTranslation pattern (catalog/models.py): translated slugs
    do NOT live here — they live in Permalink. Publishing activates the translated
    Permalink; reverting to DRAFT deactivates it (404 on the translated URL, AC-104 parity).

    Fingerprint field order (translation_fingerprint): (title, body, seo_title,
    seo_description) — order-sensitive, must match pages/signals.py and any AI
    persist path exactly.
    """

    page = models.ForeignKey(
        StaticPage,
        on_delete=models.CASCADE,
        related_name="translations",
    )
    lang_code = models.CharField(
        max_length=10,
        help_text="ISO 639-1 language code — e.g. 'fr', 'pt-br'.",
    )
    title = models.CharField(max_length=255, blank=True)
    body = models.TextField(blank=True)
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
        related_name="static_page_translations",
    )
    # NULL = pure AI content; set by admin save_model when a human edits the
    # translation. Never set by the AI persist path (ADR-014 §4 parity).
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    slug_hint = models.CharField(
        max_length=255,
        blank=True,
        default="",
        help_text=(
            "Suggested slug from an AI slug_only job. Used as the initial slug for the "
            "auto-created Permalink; updated automatically when the signal fires."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def clean(self):
        from django.core.exceptions import ValidationError

        if self.store_id and self.page_id and self.store_id != self.page.store_id:
            raise ValidationError(
                "Translation store must match the static page's store."
            )

    def __str__(self):
        return f"{self.page.title} [{self.lang_code}]"

    class Meta:
        verbose_name = "static page translation"
        verbose_name_plural = "static page translations"
        unique_together = [("store", "page", "lang_code")]


def static_page_fingerprint(page_or_translation) -> str:
    """
    Return the SHA-256 hex fingerprint of a StaticPage's (or translation's) source
    fields, in the fixed order (title, body, seo_title, seo_description).

    Accepts either a StaticPage or a StaticPageTranslation-shaped object (anything
    with .title/.body/.seo_title/.seo_description attributes).
    """
    return translation_fingerprint(
        page_or_translation.title or "",
        page_or_translation.body or "",
        page_or_translation.seo_title or "",
        page_or_translation.seo_description or "",
    )


class ContactMessageStatus(models.TextChoices):
    NEW = "new", "New"
    REPLIED = "replied", "Replied"
    CLOSED = "closed", "Closed"


class ContactMessage(StoreOwnedModel):
    """
    A message submitted via the store's contact form (ADR-018 D3).

    Invariants:
    - page is provenance-only (which StaticPage the visitor submitted from);
      SET_NULL so deleting the contact page keeps message history.
    - No IP address is persisted (data-minimal; anti-spam throttling is cache-based,
      see pages/antispam.py — it does not need storage).
    - lang_code is request.locale at submit time — used to reply in the visitor's language.
    """

    page = models.ForeignKey(
        StaticPage,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="contact_messages",
    )
    name = models.CharField(max_length=200, blank=True, default="")
    email = models.EmailField()
    subject = models.CharField(max_length=255, blank=True, default="")
    message = models.TextField()
    lang_code = models.CharField(max_length=10, blank=True, default="")
    status = models.CharField(
        max_length=20,
        choices=ContactMessageStatus.choices,
        default=ContactMessageStatus.NEW,
    )
    created_at = models.DateTimeField(auto_now_add=True)

    def __str__(self):
        return f"ContactMessage({self.email}, subject={self.subject!r})"

    class Meta:
        verbose_name = "contact message"
        verbose_name_plural = "contact messages"
        indexes = [
            models.Index(fields=["store", "status"]),
            models.Index(fields=["store", "created_at"]),
        ]
