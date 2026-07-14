"""
Static page seeding (ADR-018 D5).

Shared by:
  - pages/signals.py `seed_static_pages_for_new_store` (post_save on Store, created=True)
    — covers every future store at its only creation point.
  - `manage.py seed_static_pages` management command — idempotent backfill for
    existing stores and repair of accidental deletions.

All seeded pages are `is_published=False` (drafts create no Permalink, so no thin/
duplicate placeholder content is ever crawlable — publishing is an explicit
store-admin act). get_or_create() on (store, slug) makes re-running safe.
"""

from pages.models import PLACEHOLDER_MARKER, StaticPage, StaticPageKind

SEED_PAGES = [
    {
        "slug": "about-us",
        "title": "About us",
        "kind": StaticPageKind.GENERIC,
        "show_in_header": False,
        "show_in_footer": True,
    },
    {
        "slug": "contact",
        "title": "Contact",
        "kind": StaticPageKind.CONTACT,
        "show_in_header": True,
        "show_in_footer": True,
    },
    {
        "slug": "request-a-quote",
        "title": "Request a quote",
        "kind": StaticPageKind.QUOTATION,
        "show_in_header": False,
        "show_in_footer": True,
    },
    {
        "slug": "shipping-policy",
        "title": "Shipping policy",
        "kind": StaticPageKind.POLICY,
        "show_in_header": False,
        "show_in_footer": True,
    },
    {
        "slug": "terms-of-service",
        "title": "Terms of service",
        "kind": StaticPageKind.POLICY,
        "show_in_header": False,
        "show_in_footer": True,
    },
    {
        "slug": "privacy-policy",
        "title": "Privacy policy",
        "kind": StaticPageKind.POLICY,
        "show_in_header": False,
        "show_in_footer": True,
    },
    {
        "slug": "refund-policy",
        "title": "Refund policy",
        "kind": StaticPageKind.POLICY,
        "show_in_header": False,
        "show_in_footer": True,
    },
]


def _placeholder_body(title: str) -> str:
    return f"<p>{title} — placeholder content. {PLACEHOLDER_MARKER}</p>"


def seed_pages_for_store(store) -> int:
    """
    Create any of the 7 seed StaticPages missing for `store`, in the store's default
    language, as unpublished drafts. Idempotent — safe to call any number of times.

    Returns the number of pages actually created (0 on a fully-seeded store).
    """
    created_count = 0
    for position, spec in enumerate(SEED_PAGES):
        _page, created = StaticPage.objects.for_store(store).get_or_create(
            slug=spec["slug"],
            defaults={
                "store": store,
                "title": spec["title"],
                "body": _placeholder_body(spec["title"]),
                "kind": spec["kind"],
                "is_published": False,
                "show_in_header": spec["show_in_header"],
                "show_in_footer": spec["show_in_footer"],
                "nav_position": position,
            },
        )
        if created:
            created_count += 1
    return created_count
