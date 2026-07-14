"""
Data migration: strip the `products/` prefix from Permalink.slug and
SlugRedirect.from_slug / to_slug (ADR-014 §10-G1).

Background
----------
Prior to this fix, product canonical URLs were stored with a `products/` prefix
(e.g. `products/my-shirt`).  ADR-005 / ADR-008 require root-level product slugs
(`my-shirt`), matching how storefront resolvers receive the path after language-
prefix stripping.

This migration rewrites any row whose slug *starts with* `products/` to the
prefix-stripped form.  Collection rows (`collections/…`) are not touched.

The migration is NOT safely reversible: after stripping, there is no reliable
way to distinguish former product slugs from other root-level slugs without
inspecting the generic FK content_type, which is unavailable on SlugRedirect.
The reverse is therefore a no-op.
"""

from django.db import migrations
from django.db.models.functions import Substr

PREFIX = "products/"
# Substr is 1-indexed in Django.  len("products/") == 9, so position 10
# is the first character after the prefix.
START_POS = len(PREFIX) + 1  # 10


def fix_product_paths(apps, schema_editor):
    Permalink = apps.get_model("permalinks", "Permalink")
    SlugRedirect = apps.get_model("permalinks", "SlugRedirect")

    Permalink.objects.filter(slug__startswith=PREFIX).update(
        slug=Substr("slug", START_POS),
    )

    SlugRedirect.objects.filter(from_slug__startswith=PREFIX).update(
        from_slug=Substr("from_slug", START_POS),
    )
    SlugRedirect.objects.filter(to_slug__startswith=PREFIX).update(
        to_slug=Substr("to_slug", START_POS),
    )


def noop_reverse(apps, schema_editor):
    # Cannot safely reverse without knowing which plain slugs came from products.
    pass


class Migration(migrations.Migration):

    dependencies = [
        ("permalinks", "0003_alter_permalink_slug"),
    ]

    operations = [
        migrations.RunPython(fix_product_paths, reverse_code=noop_reverse),
    ]
