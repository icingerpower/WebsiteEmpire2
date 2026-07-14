"""
Data migration: populate StoreDomain and StoreLanguage from existing Store rows (ADR-008 §4).

For each Store:
  - If the store has a custom_domain: create two StoreDomain rows — custom domain
    as primary, subdomain as secondary (active, non-primary).
  - Otherwise: create one StoreDomain for the platform subdomain (primary).
  - Create one StoreLanguage from Store.primary_language, attached to the primary domain,
    use_path_prefix=False (root language), is_default=True.

No ShippingCountry rows are created — there is no source data; the launch-readiness
warning surfaces this gap per store instead of inventing markets silently (ADR-008 §4).

Reverse: delete all StoreDomain, StoreLanguage, ShippingCountry rows created here.
"""

from django.conf import settings
from django.db import migrations


def populate_domains(apps, schema_editor):
    Store = apps.get_model("stores", "Store")
    StoreDomain = apps.get_model("stores", "StoreDomain")
    StoreLanguage = apps.get_model("stores", "StoreLanguage")

    apex = getattr(settings, "PLATFORM_APEX_DOMAIN", None)
    if not apex:
        raise RuntimeError(
            "PLATFORM_APEX_DOMAIN setting is required for this migration. "
            "Set it to your platform subdomain apex domain (e.g. 'pradize.com')."
        )

    for store in Store.objects.all():
        subdomain_host = f"{store.subdomain}.{apex}"

        if store.custom_domain:
            # Custom domain is primary; the platform subdomain stays active, non-primary.
            custom = StoreDomain.objects.create(
                store=store,
                host=store.custom_domain.lower().split(":")[0].rstrip("."),
                is_primary=True,
                is_active=store.is_active,
            )
            StoreDomain.objects.create(
                store=store,
                host=subdomain_host,
                is_primary=False,
                is_active=store.is_active,
            )
            primary_domain = custom
        else:
            primary_domain = StoreDomain.objects.create(
                store=store,
                host=subdomain_host,
                is_primary=True,
                is_active=store.is_active,
            )

        # One StoreLanguage from Store.primary_language, attached to the primary domain.
        # use_path_prefix=False: the only language on this domain, so served at root ("/").
        lang = store.primary_language or "en"
        StoreLanguage.objects.create(
            store=store,
            domain=primary_domain,
            lang_code=lang,
            use_path_prefix=False,
            is_default=True,
            is_enabled=True,
        )
        # No ShippingCountry rows — no source data; launch-readiness check surfaces the gap.


def reverse_domains(apps, schema_editor):
    """Drop all rows created by this migration (additive — no other state was written)."""
    apps.get_model("stores", "ShippingCountry").objects.all().delete()
    apps.get_model("stores", "StoreLanguage").objects.all().delete()
    apps.get_model("stores", "StoreDomain").objects.all().delete()


class Migration(migrations.Migration):

    dependencies = [
        ("stores", "0007_store_review_request_delay_days"),
    ]

    operations = [
        migrations.RunPython(populate_domains, reverse_code=reverse_domains),
    ]
