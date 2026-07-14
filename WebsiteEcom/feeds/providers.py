"""
Concrete FeedProvider implementations (ADR-026 D1/D4).

Two day-1 providers, sharing one field map except for a single row
(g:identifier_exists, Google only — Meta needs nothing there). Adding a third
provider (Pinterest, ADR-026 PENDING P-1: "same item schema as Google") means
one subclass here + one register() call — no call-site edits anywhere else
(design-pattern-ideas §IX).

Frozen keys — must equal feeds.models.FeedProviderChoices.values AND the
/feeds/<key>/... URL segment (ADR-026 D1). A test in feeds/tests/test_registry.py
pins this equality.
"""

from feeds.registry import FeedProvider, register

# Shared canonical field order (ADR-026 D4 field-mapping table). Both
# providers emit every row here identically — per-provider deltas are DATA
# (see the two subclasses below), never a second copy of this list.
_SHARED_FIELD_MAP: list[tuple[str, str]] = [
    ("id", "g:id"),
    ("item_group_id", "g:item_group_id"),
    ("title", "g:title"),
    ("description", "g:description"),
    ("link", "g:link"),
    ("image_link", "g:image_link"),
    ("additional_image_links", "g:additional_image_link"),
    ("price", "g:price"),
    ("sale_price", "g:sale_price"),
    ("availability", "g:availability"),
    ("availability_date", "g:availability_date"),
    ("condition", "g:condition"),
    ("brand", "g:brand"),
    ("mpn", "g:mpn"),
]


class GoogleShoppingProvider(FeedProvider):
    key = "google"
    name = "Google Shopping"
    required_settings: list[str] = []
    # Google's documented escape hatch for identifier-less products (D4) —
    # Meta does not use this attribute at all.
    field_map = _SHARED_FIELD_MAP + [("identifier_exists", "g:identifier_exists")]


class FacebookDPAProvider(FeedProvider):
    key = "facebook"
    name = "Facebook Dynamic Product Ads"
    required_settings: list[str] = []
    field_map = list(_SHARED_FIELD_MAP)


register(GoogleShoppingProvider())
register(FacebookDPAProvider())
