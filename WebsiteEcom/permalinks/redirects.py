"""
Shared SlugRedirect creation logic (ADR-005 §6, §XV-4).

Extracted from permalinks/signals.py so that BOTH slug-rename paths get identical
loop-rejection / chain-collapse / creation semantics from one implementation:

- Default-language slug changes (Product/Collection/StaticPage) via the
  `slug_changed` signal → permalinks/signals.py `handle_slug_change`.
- Translated-language slug changes (ProductTranslation/CollectionTranslation/
  StaticPageTranslation `slug_hint` updates) via catalog/signals.py
  `_sync_translation_permalink`.

Before this extraction, only the default-language path created a SlugRedirect —
translated slugs changed silently with no redirect, so old translated URLs 404'd
instead of 301ing (STATIC_PAGES_SEO_REVIEW.md H2).  Fixing it once here, rather than
duplicating the logic in catalog/signals.py, guarantees both paths can never drift.
"""

from django.core.exceptions import ValidationError

from permalinks.models import RedirectTrigger, RedirectType, SlugRedirect


def _would_create_loop(
    store,
    from_slug: str,
    from_lang: str,
    to_slug: str,
    to_lang: str,
) -> bool:
    """
    Return True if creating a redirect from_slug→to_slug would form a redirect cycle.

    Traverses the existing redirect chain starting from (to_lang, to_slug).
    If the chain ever reaches (from_lang, from_slug), a loop would result.

    With proper chain-collapse, the redirect table is always flat (no chains exist after
    each write), so this is typically a single-step check.  The full traversal is kept
    for correctness in edge cases (e.g. rows inserted before chain-collapse was active).

    Uses cross_store_unsafe() to bypass the StoreScopedManager isolation guard in signal
    context; the store filter is applied explicitly.
    """
    visited: set[tuple[str, str]] = set()
    current_lang, current_slug = to_lang, to_slug
    while True:
        if (current_lang, current_slug) in visited:
            return True  # internal cycle in existing chain
        if current_lang == from_lang and current_slug == from_slug:
            return True  # would redirect back to the from side
        visited.add((current_lang, current_slug))
        try:
            redirect = SlugRedirect.objects.cross_store_unsafe().get(
                store=store,
                from_lang=current_lang,
                from_slug=current_slug,
                is_active=True,
            )
            current_lang, current_slug = redirect.to_lang, redirect.to_slug
        except SlugRedirect.DoesNotExist:
            return False


def create_slug_redirect(
    store,
    old_slug: str,
    new_slug: str,
    lang: str,
    skip_redirect: bool = False,
    redirect_type: str = RedirectType.PERMANENT,
    trigger: str = RedirectTrigger.SLUG_CHANGE,
) -> None:
    """
    Record a slug change as a SlugRedirect(old_slug → new_slug) for one (store, lang),
    applying the same write-time invariants regardless of which model/path triggered it:

    1. No-op guard — nothing to do when the slug did not actually change.
    2. Loop rejection — raises ValidationError instead of creating a cycle.  Must run
       before any redirect write so the existing chain is unmodified when traversed.
    3. Chain-collapse — every existing X → old_slug redirect becomes X → new_slug, so
       the table stays flat (no chains) after every write (ADR-005 §6).
    4. Stale-row cleanup — deletes any leftover old_slug → * row (defensive; chain
       collapse above already repointed anything that referenced old_slug as a target).
    5. Creation of old_slug → new_slug, unless the caller opted out via skip_redirect
       (e.g. ProductAdmin's skip_redirect checkbox, Finding #12).

    redirect_type / trigger (TICKET-042 / ADR-028 §2): default to the original
    slug-rename semantics (301 permanent, trigger=slug_change).  ProductPageVersion
    deactivation/deletion (permalinks/registry.py deactivate_page_version_permalinks /
    delete_page_version_permalinks) reuse this SAME function — the one place loop
    rejection + chain-collapse is implemented — passing redirect_type=TEMPORARY (302,
    deactivate) or PERMANENT (301, delete) and trigger=PAGE_VERSION_CHANGE, rather than
    duplicating this logic for a "different page" redirect (version → primary).

    Callers are responsible for:
    - Wrapping this call in the same atomic transaction as the Permalink slug update.
    - Only calling this when the OLD slug was ever active/routable — a permalink that
      was never active (e.g. an unpublished translation) had no live URL to redirect
      from, so calling this for it would manufacture a 301 for a URL nobody could have
      visited (mirrors the default-language path, which only fires from a pre_save
      slug_changed signal on an already-persisted row).
    """
    if old_slug == new_slug:
        return

    if _would_create_loop(store, old_slug, lang, new_slug, lang):
        raise ValidationError(
            f"Redirect loop detected: creating '{old_slug}' → '{new_slug}' "
            f"would form a cycle.  Delete the conflicting redirect first."
        )

    # Chain-collapse: every X → old_slug becomes X → new_slug.
    SlugRedirect.objects.cross_store_unsafe().filter(
        store=store,
        to_lang=lang,
        to_slug=old_slug,
    ).update(to_slug=new_slug)

    # Remove any existing redirect with from_slug=old_slug (defensive).  After
    # chain-collapse above, upstream pointers to old_slug are already updated, so
    # deleting this row is safe and keeps the table flat.
    SlugRedirect.objects.cross_store_unsafe().filter(
        store=store,
        from_lang=lang,
        from_slug=old_slug,
    ).delete()

    if not skip_redirect:
        SlugRedirect(
            store=store,
            from_slug=old_slug,
            from_lang=lang,
            to_slug=new_slug,
            to_lang=lang,
            redirect_type=redirect_type,
            is_active=True,
            auto_created=True,
            trigger=trigger,
        ).save()
