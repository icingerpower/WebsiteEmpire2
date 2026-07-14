"""
Frozen employee-permission module vocabulary (ADR-033 D1).

MODULES is the registry of record for the 19-row permission matrix (AF-002's
18 modules + the "employees" 19th row, D1c). Keys are stable identifiers —
NEVER rename a key to match a display-label change; only the label may change
freely (design-pattern-ideas.txt §XV / §VII: never use display values as
identity). A key already shipped in code is canonical; the AF-002 label is
the display name only (freeze criterion, ADR-033 D1a) — hence `pages`
(label "CMS") and `analytics` (label "Reports") keep their shipped keys.

`collections` was merged into `products` by the one-time reversible data
migration `stores/migrations/0xxx_merge_collections_into_products.py`
(ADR-033 D1b) — `collections` is not, and must never again be, a MODULES key.

"Invoice Orders", "CSV Templates", "Files", "Zapier" are removed from the
engine per AF-002's owner annotations — they never enter the vocabulary.
"""

from collections import namedtuple

Module = namedtuple("Module", "key label levels")

MODULES = (
    Module("apps", "Apps", ("full", "none")),
    Module("pages", "CMS", ("full", "none")),
    Module("customers", "Customers", ("full", "none")),
    Module("dashboard", "Dashboard", ("full", "none")),
    Module("domains", "Domains", ("full", "none")),
    Module("gift_cards", "Gift cards", ("full", "none")),
    Module("inventory", "Inventory", ("full", "none")),
    Module("orders", "Orders", ("full", "limited", "none")),
    Module("products", "Products & Collections", ("full", "none")),
    Module("analytics", "Reports", ("full", "none")),
    Module("settings", "Settings", ("full", "none")),
    Module("themes", "Themes", ("full", "none")),
    Module("upsell_campaigns", "Up-sell campaigns", ("full", "none")),
    Module("abandoned_campaigns", "Abandoned campaigns", ("full", "none")),
    Module("pixels", "Pixels", ("full", "none")),
    Module("reviews", "Reviews", ("full", "none")),
    Module("currency_converter", "Currency Converter", ("full", "none")),
    Module("security_badges", "Security Badge", ("full", "none")),
    # 19th row (ADR-033 D1c — DECIDED, human 2026-07-11: separate row,
    # matching shipped code which already gates stores/admin.py on
    # "employees"). If a future need arises, hide this row and map employees
    # -> settings in stores/permissions.py's resolver — a one-line registry
    # change.
    Module("employees", "Employee Accounts", ("full", "none")),
)

MODULES_BY_KEY = {module.key: module for module in MODULES}

#: Keys with no ModelAdmin surface today (ADR-033 D3c): gated via
#: `require_module()` on custom views (dashboard widgets, the analytics
#: report page) rather than via a registered ModelAdmin. The drift test's
#: "every key is claimed" assertion allowlists these.
VIEW_ONLY_MODULES = frozenset({"dashboard", "analytics"})

#: Keys reserved for a future admin screen (ADR-033 D3c) — per-variant
#: inventory (folded into `products` for now), store theme selection
#: (TICKET-029), and the customers app when it grows a store-site admin.
#: Excluded from the drift test's "every key is claimed" assertion.
RESERVED_MODULES = frozenset({"inventory", "themes", "customers"})

#: Sentinel `module_key` value: this ModelAdmin is intentionally exempt from
#: matrix gating. Every use requires a justification comment at the
#: declaration site (ADR-033 D3b). Sole shipped case: core's read-only
#: own-profile User admin.
MODULE_EXEMPT = "__module_exempt__"

#: Display labels for a permission level, used by the grid widget (TICKET-047).
LEVEL_LABELS = {
    "full": "Full access",
    "limited": "Limited access",
    "none": "No access",
}


def module_choices(key):
    """Return the (value, label) choices for one module's grid radio group."""
    return [(level, LEVEL_LABELS[level]) for level in MODULES_BY_KEY[key].levels]
