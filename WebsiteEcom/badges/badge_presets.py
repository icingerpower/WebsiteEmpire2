"""
Security badge preset registry (ADR-030 D3, D1).

A code registry, not DB rows — the exact shape of
`engagement.themes.OVERLAY_THEMES` (ADR-027 D10): adding a preset means
adding a template partial + a preview SVG + one entry here, no migration.

Every preset template `{% include %}`s the single shared partial
`storefront/partials/security_badges/_payment_logos.html` instead of
hardcoding its own logo list. That partial reads `accepted_families`
(computed by `accepted_families()` below) so a preset never claims to accept
a payment network the store does not actually have enabled — the
truthful-by-construction constraint that is this ADR's reason for existing
(§1). Self-verifying "secure connection" icons (lock/shield, SSL seal) are
gated separately, in `badges.slot_provider`, on `request.is_secure()`.
"""

from django.core.cache import cache

from payments.models import PaymentMethod

# PaymentMethod rows change on the order of "months," not "requests" (ADR-030
# §3) — a short cache keeps this decorative, non-transactional lookup off the
# hot path of every product/cart/checkout page render.
ACCEPTED_FAMILIES_CACHE_KEY = "security_badge:accepted_families"
ACCEPTED_FAMILIES_CACHE_TTL_SECONDS = 300

# key -> {
#   "template": <partial path under storefront/templates/>,
#   "name": <admin label>,
#   "preview": <static-relative path to a schematic SVG wireframe of the
#              composition, rendered via badges.widgets.BadgePresetSelect>,
# }
BADGE_PRESETS = {
    "trust_and_cards": {
        "template": "storefront/partials/security_badges/trust_and_cards.html",
        "name": "Trusted & secure + accepted cards",
        "preview": "badges/previews/trust_and_cards.svg",
    },
    "seal_and_cards": {
        "template": "storefront/partials/security_badges/seal_and_cards.html",
        "name": "SSL seal + accepted cards",
        "preview": "badges/previews/seal_and_cards.svg",
    },
    "minimal_lock": {
        "template": "storefront/partials/security_badges/minimal_lock.html",
        "name": "Minimal lock icon",
        "preview": "badges/previews/minimal_lock.svg",
    },
    "payment_logos_only": {
        "template": "storefront/partials/security_badges/payment_logos_only.html",
        "name": "Accepted payment methods only",
        "preview": "badges/previews/payment_logos_only.svg",
    },
}


def badge_preset_choices():
    """Return (key, name) choice tuples for the admin ModelForm, in registry order."""
    return [(key, meta["name"]) for key, meta in BADGE_PRESETS.items()]


def accepted_families() -> set:
    """
    Platform-wide set of currently-enabled `PaymentMethod.method_family`
    values (ADR-030 §3).

    Deliberately platform-level, not a per-store `payments.routing.preview_route`
    evaluation — that call needs country/currency/amount, none of which exist
    for a cosmetic badge shown to an anonymous visitor, and would add real
    query cost to every product/cart page view. See ADR-030 §3 "Options
    considered" for the full rationale; this is the accepted, narrow
    inaccuracy (a family enabled platform-wide but momentarily unrouteable for
    one store's org) traded for correctness on the case this ADR exists to
    prevent (never showing a certification/service the store does not have).
    """
    cached = cache.get(ACCEPTED_FAMILIES_CACHE_KEY)
    if cached is not None:
        return cached
    families = set(
        PaymentMethod.objects.filter(is_enabled=True).values_list("method_family", flat=True)
    )
    cache.set(ACCEPTED_FAMILIES_CACHE_KEY, families, ACCEPTED_FAMILIES_CACHE_TTL_SECONDS)
    return families
