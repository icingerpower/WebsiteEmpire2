"""
Single consent-resolution function (ADR-025 D2/D3, §XV-4 design-pattern-ideas).

consent.state.get_consent(request) -> ConsentState is the ONLY place that
parses the `pradize_consent` cookie. Every consumer (PixelsSlotProvider,
claim_purchase_pixels' caller, the analytics beacon tag, ConsentSlotProvider
itself) calls this function — nobody else parses the cookie independently.

consent.state.resolve_consent_for_pixels(request) -> ConsentState is the ONE
place (also §XV-4) that layers the store's non-EU acknowledgment escape
hatch (ConsentSettings.is_enabled/non_eu_acknowledged, D6) on top of
get_consent()'s cookie-derived state, for the two pixel-facing consumers
that need it: PixelsSlotProvider.render() and the claim_purchase_pixels()
caller in storefront/views_checkout.py (RM-1 fix — see its docstring below).
The analytics beacon tag and ConsentSlotProvider's own banner render are
deliberately NOT routed through it: the beacon has its own narrower D4 gate
(am_objected / beacon_requires_consent, unaffected by RM-1) and the banner
must keep reflecting the shopper's actual cookie state, not the escape
hatch (ConsentSlotProvider never renders at all when is_enabled=False, via
its own is_enabled() check, regardless of the acknowledgment).

Fail-closed parsing (ADR-025 D2): any malformed value, unknown version, or
policy_version != CONSENT_POLICY_VERSION is treated as UNDECIDED. The regex
below is fully anchored with bounded, non-nested quantifiers (no
catastrophic-backtracking risk), and a non-matching value of any length or
shape simply falls through to UNDECIDED_STATE without raising.

Rollback shim (ADR-025 "Rollback strategy"): if the `consent` app is ever
removed from the codebase entirely (not just INSTALLED_APPS — the Python
package itself), `from consent.state import get_consent` (or
`resolve_consent_for_pixels`) at every call site outside this app would
raise ImportError. Each of those call sites wraps the import in its own
try/except and falls back to a local, independently defined fail-closed
stand-in (never importing anything from this package in the except
branch) — see pixels/slot_provider.py, storefront/views_checkout.py,
storefront/templatetags/storefront_tags.py. This module itself needs no shim:
if `consent` is gone, this file is gone too.
"""

import logging
import re
from dataclasses import dataclass

from consent.policy import (
    CATEGORY_ANALYTICS,
    CATEGORY_MARKETING,
    CATEGORY_NECESSARY,
    CONSENT_COOKIE_NAME,
    CONSENT_POLICY_VERSION,
)

logger = logging.getLogger(__name__)

# v<version>:<32 hex chars>:a=<0|1>,m=<0|1>,am=<0|1> — e.g. "v1:6f9c...e2:a=1,m=0,am=1".
_COOKIE_RE = re.compile(
    r"^v(?P<version>[0-9]{1,4}):"
    r"(?P<consent_id>[0-9a-f]{32}):"
    r"a=(?P<analytics>[01]),m=(?P<marketing>[01]),am=(?P<am_objected>[01])$"
)


@dataclass(frozen=True)
class ConsentState:
    """
    Tri-state-per-category consent snapshot (ADR-025 D1/D3).

    decided=False means "undecided" — banner should show, allows() returns
    False for both consent-requiring categories regardless of the analytics/
    marketing field values (which are meaningless when undecided; kept at
    False by construction in UNDECIDED_STATE).
    """

    decided: bool
    analytics: bool
    marketing: bool
    am_objected: bool

    def allows(self, category: str) -> bool:
        """
        Return whether `category` may render/fire right now.

        "necessary" is always True (exempt, never a decision point).
        Undecided ⇒ False for analytics/marketing (§XV-3: absence of a
        decision is never treated as consent).
        Unknown category strings return False rather than raise — a typo'd
        category name in calling code must never accidentally grant a
        tracker; it is logged loudly instead (§XV-1: loud, not silent —
        the log line surfaces the bug without turning a render-path call
        into a 500).
        """
        if category == CATEGORY_NECESSARY:
            return True
        if not self.decided:
            return False
        if category == CATEGORY_ANALYTICS:
            return self.analytics
        if category == CATEGORY_MARKETING:
            return self.marketing
        logger.error("ConsentState.allows() called with unknown category %r; refusing.", category)
        return False


# Undecided/refused-equivalent for gating purposes (allows() is identical for
# both -- False for analytics/marketing) -- used for: no cookie, malformed
# cookie, and the cross-app rollback fallback.
UNDECIDED_STATE = ConsentState(decided=False, analytics=False, marketing=False, am_objected=False)

# Fully-granted state for the non-EU acknowledgment escape hatch (RM-1 fix,
# ADR-025 D6/P3 dated correction 2026-07-11) -- see
# resolve_consent_for_pixels() below. am_objected=False because this state
# only feeds the pixel-gating path (analytics/marketing categories); the
# audience-measurement beacon (D4) is a separate, narrower gate untouched by
# this fix.
ALL_ALLOWED_STATE = ConsentState(decided=True, analytics=True, marketing=True, am_objected=False)


def serialize_cookie_value(
    consent_id_hex: str,
    *,
    analytics: bool,
    marketing: bool,
    am_objected: bool,
    policy_version: int = CONSENT_POLICY_VERSION,
) -> str:
    """
    Build the compact cookie value written by POST /_consent/ (ADR-025 D2).

    consent_id_hex must be a 32-char lowercase hex string (uuid.uuid4().hex).
    """
    return (
        f"v{policy_version}:{consent_id_hex}:"
        f"a={int(analytics)},m={int(marketing)},am={int(am_objected)}"
    )


def get_consent(request) -> ConsentState:
    """
    Parse the `pradize_consent` cookie off `request` and return a ConsentState.

    Fail-closed (ADR-025 D2): missing cookie, malformed value, or a
    policy_version that does not match the current platform constant all
    resolve to UNDECIDED_STATE — never raises.
    """
    raw = request.COOKIES.get(CONSENT_COOKIE_NAME, "")
    if not raw:
        return UNDECIDED_STATE

    match = _COOKIE_RE.match(raw)
    if not match:
        return UNDECIDED_STATE

    if int(match.group("version")) != CONSENT_POLICY_VERSION:
        return UNDECIDED_STATE

    return ConsentState(
        decided=True,
        analytics=match.group("analytics") == "1",
        marketing=match.group("marketing") == "1",
        am_objected=match.group("am_objected") == "1",
    )


def resolve_consent_for_pixels(request) -> ConsentState:
    """
    Single resolution point for pixel-facing consent gating (RM-1 fix,
    `docs/releases/consent-management/KNOWN_RISKS.md` item 1; ADR-025 D3
    dated correction 2026-07-11).

    The bug this closes: `get_consent(request)` alone answers "what did
    THIS shopper's cookie say", which is UNDECIDED forever on a store that
    has legitimately disabled the banner via the non-EU acknowledgment
    (`ConsentSettings.is_enabled=False, non_eu_acknowledged=True`, D6) --
    no shopper on that store is ever shown the banner, so no shopper ever
    sets the `pradize_consent` cookie, so gating pixels on the cookie alone
    leaves them permanently dark. ADR-025 D3 ("when consent is enabled for
    the store...") and P3 (the acknowledgment as the documented remedy for
    a pixel-volume drop) both intend the opposite: with the acknowledgment
    signed, pixels fire unconditionally -- the pre-consent-feature
    behaviour -- because the store owner has taken responsibility for
    tracker compliance instead of the platform gating it.

    Resolution: cookie-derived state is used as-is UNLESS the store's
    `ConsentSettings` show is_enabled=False AND non_eu_acknowledged=True,
    in which case ALL_ALLOWED_STATE is returned (every category, including
    ones the cookie never granted, resolves to allowed).

    Deliberately NOT special-cased: is_enabled=False WITHOUT the
    acknowledgment. That combination is the launch-check-blocked state
    (`ConsentSlotProvider.validate_settings`, D6 -- "Active pixels require
    the consent banner (or an explicit non-EU acknowledgment)") and must
    stay fail-closed (dark) if it ever reaches production despite the
    launch check; this function only lifts the gate for the one combination
    ADR-025 explicitly documents as an intentional, acknowledged opt-out.

    This is the ONE place (SS XV-4) both pixels/slot_provider.py's
    render-path gate and storefront/views_checkout.py's
    claim_purchase_pixels() caller resolve consent from -- neither
    duplicates the ConsentSettings lookup or the disabled+acknowledged
    condition itself; both simply call this function (each behind its own
    existing ImportError rollback shim -- see those modules' docstrings)
    and derive whatever shape they need (a ConsentState for the slot
    provider's `.allows()` calls, a frozenset of allowed category keys for
    the claim path) from its result.

    store is read off `request.store` (both callers already have it there
    by the time they call this). If it is absent, falls back to the plain
    cookie-derived state -- matches get_consent()'s own fail-closed
    posture; there is no store to look up an escape hatch for.
    """
    consent = get_consent(request)

    store = getattr(request, "store", None)
    if store is None:
        return consent

    from consent.models import get_or_create_consent_settings

    settings_row = get_or_create_consent_settings(store)
    if not settings_row.is_enabled and settings_row.non_eu_acknowledged:
        return ALL_ALLOWED_STATE

    return consent
