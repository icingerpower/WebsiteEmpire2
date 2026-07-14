"""
ADR-033 D1b/D1d: freeze the 19-module permission vocabulary on existing data.

Two independent, reversible cleanups on StoreEmployee.permissions_json:

1. Merge the one true vocabulary conflict: "collections" -> "products".
   "Max access wins": if either key already grants "full", the merged
   "products" value is "full" (an employee who could edit Collections must
   not silently lose that ability just because the two rows were unified —
   losing previously-granted access is a worse outcome than the row
   temporarily covering a slightly broader module than it did before).
   Reverse: re-adds "collections" copied from the (already merged)
   "products" value — a best-effort reverse per ADR-033's rollback
   strategy; the original PRE-merge "collections" value is not recoverable
   (the two keys become one by design), but this keeps every row valid
   against the pre-ADR-033 vocabulary if TICKET-054 is ever rolled back.

2. Strip any other unknown/legacy key from permissions_json so every
   existing row passes StoreEmployee.clean()'s new vocabulary validation
   the first time it is saved through the app after this migration runs
   (D1d: "A data migration first strips legacy/unknown keys so existing
   rows pass"). The valid-key list is hardcoded here (never import
   stores.modules into a migration — MODULES can change after this
   migration is written, but a migration's behavior must not).
   Reverse is a no-op for this part: an unknown key's original value is not
   recoverable, and letting it silently reappear on rollback would just
   reintroduce the exact validation gap TICKET-054 closes.
"""

from django.db import migrations

# Snapshot of stores.modules.MODULES_BY_KEY keys AT THE TIME this migration
# was written (ADR-033 D1a) -- deliberately hardcoded, not imported.
_VALID_KEYS = frozenset({
    "apps", "pages", "customers", "dashboard", "domains", "gift_cards",
    "inventory", "orders", "products", "analytics", "settings", "themes",
    "upsell_campaigns", "abandoned_campaigns", "pixels", "reviews",
    "currency_converter", "security_badges", "employees",
})

_RANK = {"full": 0, "limited": 1, "none": 2}


def _more_permissive(level_a, level_b):
    rank_a = _RANK.get(level_a, _RANK["none"])
    rank_b = _RANK.get(level_b, _RANK["none"])
    return level_a if rank_a <= rank_b else level_b


def merge_collections_and_strip_unknown_keys(apps, schema_editor):
    StoreEmployee = apps.get_model("stores", "StoreEmployee")
    for employee in StoreEmployee.objects.all().iterator():
        perms = dict(employee.permissions_json or {})
        changed = False

        if "collections" in perms:
            collections_level = perms.pop("collections")
            products_level = perms.get("products", "none")
            perms["products"] = _more_permissive(products_level, collections_level)
            changed = True

        for key in list(perms.keys()):
            if key not in _VALID_KEYS:
                del perms[key]
                changed = True

        if changed:
            employee.permissions_json = perms
            employee.save(update_fields=["permissions_json"])


def reverse_merge(apps, schema_editor):
    """Best-effort reverse: re-add 'collections' mirroring 'products'."""
    StoreEmployee = apps.get_model("stores", "StoreEmployee")
    for employee in StoreEmployee.objects.all().iterator():
        perms = dict(employee.permissions_json or {})
        if "products" not in perms or "collections" in perms:
            continue
        perms["collections"] = perms["products"]
        employee.permissions_json = perms
        employee.save(update_fields=["permissions_json"])


class Migration(migrations.Migration):

    dependencies = [
        ("stores", "0012_alter_organization_coverage_areas_json"),
    ]

    operations = [
        migrations.RunPython(
            merge_collections_and_strip_unknown_keys,
            reverse_merge,
        ),
    ]
