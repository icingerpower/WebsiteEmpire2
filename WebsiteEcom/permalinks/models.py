"""
Permalink and SlugRedirect models (TICKET-016, ADR-005).

PermalinkTranslation spec (05_database_schema.md §6) is implemented as Permalink with
a lang dimension: each (store, lang, slug) triple maps to exactly one page.

The target object is identified by a generic FK (content_type + object_id) so any model
can be a permalink target without requiring a hard FK dependency on each model.

SlugRedirect stores old slug → new slug per (store, from_lang, from_slug).  Chain-collapse
(A→B + B→C ⇒ A→C) is implemented at write time in permalinks/signals.py (ADR-005 §6).
Loop creation is rejected at write time with ValidationError.

Design notes:
- Both extend StoreOwnedModel (ADR-001 §4): `store` FK, StoreScopedManager.
- resolve() in permalinks/resolver.py is the ONLY way to look up a URL; no inline URL
  construction anywhere else (ADR-005 §3, §XV-4).
- Redirects are created by the slug_changed signal consumer (permalinks/signals.py),
  never manually through the store-admin UI (DECIDED ADR-005 §6).
"""

import re

from django.contrib.contenttypes.fields import GenericForeignKey
from django.contrib.contenttypes.models import ContentType
from django.core.exceptions import ValidationError
from django.db import models

from core.models import StoreOwnedModel

# ADR-008: ISO 639-1 language codes (optionally with region suffix, e.g. 'pt-br')
# are reserved as top-level slugs to avoid shadowing language URL prefixes.
# A product slugged 'fr' would collide with '/fr/' when FR is later added as a language.
_ISO_LANG_PATTERN = re.compile(r"^[a-z]{2}(-[a-z]{2})?$")

# ADR-018 D1: fixed top-level routes registered in storefront/urls.py BEFORE the
# <path:slug> catch-all. A static page (or any other slug producer — products,
# AI slug_only jobs) slugged e.g. 'cart' would silently never resolve (§XV-1
# invisible failure) because the fixed route always wins. Enforced here — the ONE
# validator every permalink producer goes through (§XV-4) — rather than per-model,
# so it protects static pages, products, and AI-generated slugs alike.
#
# Exact-value match only (NOT "first path segment of a multi-segment slug"):
# Collection permalinks legitimately use a 'collections/<slug>' prefix (registry.py
# register_collection) — a first-segment check would flag every collection. Product
# and StaticPage slugs are always single-segment (make_slug() never emits '/'), so
# exact matching already covers the real collision risk (a page/product slugged
# exactly 'cart', 'products', etc.) without rejecting legitimately prefixed slugs.
#
# Risk (ADR-018): a new fixed route added to storefront/urls.py must add its first
# path segment here in the SAME PR — see the reserved-set-covers-urlpatterns drift
# test (pages/tests/test_reserved_slugs.py), generated from storefront.urls.urlpatterns
# so this set cannot silently drift out of sync.
RESERVED_TOP_LEVEL_SLUGS = frozenset({
    "cart", "checkout", "search", "orders", "products", "collections",
    "admin", "campaigns", "sitemap.xml", "robots.txt", "_analytics",
    # ADR-024, TICKET-038: /chat/session/ and /chat/message/, registered in
    # webecom/urls.py BEFORE the storefront catch-all (not in storefront/urls.py
    # itself, so the storefront.urls-generated drift test does not cover it —
    # added here directly instead).
    "chat",
    # ADR-023 §4, TICKET-031/TICKET-037: POST /currency/set/, registered in
    # storefront/urls.py before the <path:slug> catch-all (caught automatically
    # by pages/tests/test_reserved_slugs.py's drift test — added here in the
    # same PR per that test's own requirement).
    "currency",
    # ADR-027 D1/D3, TICKET-034: POST /overlay/signup/, /overlay/event/, and
    # GET /overlay/confirm/<token>/, registered in webecom/urls.py BEFORE the
    # storefront catch-all (not in storefront/urls.py itself, so the
    # storefront.urls-generated drift test does not cover it — added here
    # directly instead, same pattern as "chat"/"feeds" — reverse("engagement:...")
    # needs a top-level namespace, not one nested under "storefront").
    "overlay",
    # ADR-026, TICKET-033: GET /feeds/<provider>/<country>-<lang>.xml, registered
    # in webecom/urls.py BEFORE the storefront catch-all (not in storefront/urls.py
    # itself, so the storefront.urls-generated drift test does not cover it —
    # added here directly instead, same pattern as "chat").
    "feeds",
})


def _validate_slug_not_reserved(value: str) -> None:
    """
    Reject any slug that matches the ISO 639-1 pattern (with optional region suffix),
    or that exactly equals a reserved top-level slug (ADR-008, ADR-018 D1).

    This prevents top-level slugs like 'fr', 'en', 'pt-br', 'cart', 'checkout' from ever
    being registered, even when the colliding language/route is not currently active.
    Adding a language or a fixed route later must never collide with an existing
    root-level slug.  See RESERVED_TOP_LEVEL_SLUGS docstring for why this is an exact
    match rather than a first-path-segment match.
    """
    if _ISO_LANG_PATTERN.match(value):
        raise ValidationError(
            f"The slug {value!r} is reserved — ISO 639-1 language codes cannot be used "
            "as slugs to avoid conflicts with language URL prefixes (ADR-008)."
        )
    if value in RESERVED_TOP_LEVEL_SLUGS:
        raise ValidationError(
            f"The slug {value!r} is reserved — it is a fixed storefront route "
            "(ADR-018 D1) and cannot be used as a page/product slug."
        )


class PermalinkTrigger(models.TextChoices):
    MANUAL = "manual", "Manual"
    SLUG_CHANGE = "slug_change", "Slug change"
    IMPORT = "import", "Import"


class RedirectType(models.TextChoices):
    PERMANENT = "permanent", "Permanent (301)"
    TEMPORARY = "temporary", "Temporary (302)"
    NONE = "none", "None (dead URL — prevents slug reuse from inheriting old redirect)"


class RedirectTrigger(models.TextChoices):
    SLUG_CHANGE = "slug_change", "Slug change"
    DOMAIN_PARK = "domain_park", "Domain park"
    IMPORT = "import", "Import"
    # TICKET-042 / ADR-028 §2: a ProductPageVersion was deactivated (302, temporary)
    # or deleted (301, permanent), redirecting its URL(s) to the primary product URL.
    PAGE_VERSION_CHANGE = "page_version_change", "Page version change"


# Maps RedirectType → HTTP status code. 'none' is intentionally absent (dead URL = no redirect).
REDIRECT_TYPE_TO_HTTP_STATUS: dict[str, int] = {
    RedirectType.PERMANENT: 301,
    RedirectType.TEMPORARY: 302,
}


def http_status_for(redirect_type: str) -> int | None:
    """
    Convert a RedirectType value to an HTTP status code.

    Returns None for redirect_type='none' (intentionally dead URL — resolver treats as 404).
    """
    return REDIRECT_TYPE_TO_HTTP_STATUS.get(redirect_type)


class Permalink(StoreOwnedModel):
    """
    Canonical URL → page mapping, one per (store, lang, slug) triple (ADR-005 §2).

    lang holds an ISO 639-1 language code (e.g. 'en', 'fr', 'de').
    slug is the full URL path without a leading slash (e.g. 'my-shirt' for a product
    or 'collections/my-collection' for a collection).

    The target object is identified by a generic FK (content_type + object_id) so any
    model can be a permalink target without requiring a hard FK dependency on each content
    type.  For special pages with no backing object (e.g. HOME), both fields are null.

    is_active=False marks a permalink superseded by a slug change or object deletion —
    kept for historical lookup only.  The resolver ignores inactive permalinks.

    auto_created=True marks rows inserted automatically by a signal or import, not by a
    human admin.  trigger records what caused the creation.
    """

    content_type = models.ForeignKey(
        ContentType,
        on_delete=models.CASCADE,
        null=True,
        blank=True,
        help_text="ContentType of the target object.  Null for special pages with no target.",
    )
    object_id = models.BigIntegerField(
        null=True,
        blank=True,
        help_text="PK of the target object.  Null for HOME.",
    )
    content_object = GenericForeignKey("content_type", "object_id")

    lang = models.CharField(
        max_length=10,
        default="en",
        help_text="ISO 639-1 language code — e.g. 'en', 'fr', 'de'.",
    )
    slug = models.CharField(
        max_length=500,
        db_index=True,
        validators=[_validate_slug_not_reserved],
        help_text="Full URL path without leading slash — e.g. 'my-shirt' (product) or 'collections/my-collection'.",
    )
    is_active = models.BooleanField(default=True)
    auto_created = models.BooleanField(
        default=False,
        help_text="True when created automatically by a signal or import.",
    )
    trigger = models.CharField(
        max_length=32,
        choices=PermalinkTrigger.choices,
        null=True,
        blank=True,
        help_text="What caused this permalink to be created.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "permalink"
        verbose_name_plural = "permalinks"
        unique_together = [("store", "lang", "slug")]
        indexes = [
            models.Index(
                fields=["store", "lang", "is_active"],
                name="prmlnk_store_lang_active_idx",
            ),
        ]

    def __str__(self) -> str:
        return f"{self.store_id}:{self.lang}:{self.slug}"

    def save(self, *args, **kwargs):
        """
        Enforce the reserved-slug / ISO-639-lang validator on EVERY write, not only
        on full_clean() (security audit F1, ADR-018 D1 §XV-4): `validators=` on a
        field only runs inside full_clean()/ModelForm validation, but every actual
        production permalink producer (pages/signals.py sync_static_page_permalink,
        permalinks/registry.py register_product/register_collection,
        catalog/signals.py _sync_translation_permalink) writes via plain .save(),
        never full_clean() — so the field validator was completely inert at runtime.

        Checking here, at the one place every producer's .save() call funnels
        through, means a future producer gets this for free without having to
        remember to call full_clean() itself. See pages/models.py StaticPage.clean()
        / StaticPage.save() for the earlier, user-facing rejection at the admin-form
        and direct-save boundary (nicer error, same underlying rule).
        """
        _validate_slug_not_reserved(self.slug)
        super().save(*args, **kwargs)

    def clean(self):
        """
        Write-time validation (ADR-008 §Options C):
        Ensure Permalink.lang is a configured StoreLanguage for this store.

        The check is skipped if no StoreLanguage rows exist for the store (migration
        period / backward compatibility — the first rows for a store may be created
        before the data migration has run).
        """
        if self.store_id and self.lang:
            from stores.models import StoreLanguage

            valid_codes = set(
                StoreLanguage.objects.filter(store_id=self.store_id)
                .values_list("lang_code", flat=True)
            )
            if valid_codes and self.lang not in valid_codes:
                raise ValidationError(
                    f"Language code {self.lang!r} is not a configured StoreLanguage "
                    "for this store."
                )


class SlugRedirect(StoreOwnedModel):
    """
    Old slug → new slug redirect, with language dimension (ADR-005 §6).

    Auto-created by the slug_changed signal consumer (permalinks/signals.py).

    Chain-collapse and loop rejection are implemented at write time in signals.py:
    - Creating from_slug→to_slug rewrites all existing X→from_slug rows to X→to_slug
      in the same transaction (chain-collapse guarantees a flat table — no chains).
    - If to_slug would redirect back to from_slug, ValidationError is raised (loop reject).

    redirect_type 'none' marks intentionally dead URLs so a reused slug never inherits an
    old redirect (ADR-005 §6).  The resolver returns None (404) for 'none' type redirects.
    """

    from_slug = models.CharField(
        max_length=500,
        db_index=True,
        help_text="Former URL path that should redirect.  Without leading slash.",
    )
    from_lang = models.CharField(
        max_length=10,
        default="en",
        help_text="Language of the redirected-from slug.",
    )
    to_slug = models.CharField(
        max_length=500,
        help_text="Current URL path to redirect to.  Without leading slash.",
    )
    to_lang = models.CharField(
        max_length=10,
        default="en",
        help_text="Language of the redirect destination slug.",
    )
    redirect_type = models.CharField(
        max_length=16,
        choices=RedirectType.choices,
        default=RedirectType.PERMANENT,
        help_text="permanent=301, temporary=302, none=intentionally dead URL.",
    )
    is_active = models.BooleanField(default=True)
    auto_created = models.BooleanField(
        default=False,
        help_text="True when created automatically by a signal or import.",
    )
    trigger = models.CharField(
        max_length=32,
        choices=RedirectTrigger.choices,
        null=True,
        blank=True,
        help_text="What caused this redirect to be created.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "slug redirect"
        verbose_name_plural = "slug redirects"
        unique_together = [("store", "from_lang", "from_slug")]

    def __str__(self) -> str:
        return f"{self.store_id}:{self.from_lang}:{self.from_slug} → {self.to_slug}"
