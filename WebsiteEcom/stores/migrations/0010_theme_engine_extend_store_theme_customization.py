"""
Extends stores.Theme with full ADR-012 D2 fields and adds StoreThemeCustomization.

Migration strategy for source_ref (unique, non-nullable):
  Step 1 — Add source_ref as nullable (safe on existing rows).
  Step 2 — Data migration: assign a unique placeholder to any existing Theme rows that
            have no source_ref, then seed the canonical 'general' Theme row.
  Step 3 — AlterField: make source_ref non-nullable and add unique=True.

This keeps the migration reversible: reverse_code clears the placeholder values so
Step 3 can be reversed to Step 1's nullable state.
"""

import django.db.models.deletion
from django.db import migrations, models


def seed_theme_source_refs(apps, schema_editor):
    """
    Assign unique source_ref placeholders to any existing Theme stubs, then
    upsert the canonical 'general' platform-default Theme row.

    Any Theme row with an empty source_ref gets a stable placeholder derived from
    its pk ('theme-<pk>') so the unique constraint in Step 3 does not fail.
    """
    Theme = apps.get_model("stores", "Theme")

    # Assign placeholders for existing rows without a source_ref
    for theme in Theme.objects.filter(source_ref=""):
        theme.source_ref = f"theme-{theme.pk}"
        theme.save(update_fields=["source_ref"])

    # Upsert the canonical 'general' Theme (platform default, status=draft)
    Theme.objects.get_or_create(
        source_ref="general",
        defaults={
            "name": "General",
            "engine": "code",
            "is_default": True,
            "status": "draft",
            "is_active": True,
        },
    )


def reverse_seed(apps, schema_editor):
    """
    Reverse: delete the seeded 'general' row and clear placeholder source_refs
    so Step 3 can be reversed to nullable without a unique-constraint clash.
    """
    Theme = apps.get_model("stores", "Theme")
    Theme.objects.filter(source_ref="general").delete()
    # Clear placeholders (pattern: 'theme-<digits>')
    import re
    _PLACEHOLDER_RE = re.compile(r"^theme-\d+$")
    for theme in Theme.objects.all():
        if _PLACEHOLDER_RE.match(theme.source_ref):
            theme.source_ref = ""
            theme.save(update_fields=["source_ref"])


class Migration(migrations.Migration):

    dependencies = [
        ("stores", "0009_alter_store_custom_domain_and_more"),
    ]

    operations = [
        # ------------------------------------------------------------------ #
        # Step 1 — Add new Theme fields (source_ref nullable for now)         #
        # ------------------------------------------------------------------ #
        migrations.AddField(
            model_name="theme",
            name="engine",
            field=models.CharField(
                choices=[("code", "Code theme (DTL)"), ("visual_builder", "Visual builder")],
                default="code",
                max_length=20,
            ),
        ),
        migrations.AddField(
            model_name="theme",
            name="source_ref",
            # nullable=True here; Step 3 removes null after the data migration
            field=models.CharField(
                blank=True,
                default="",
                help_text="Theme package directory key, e.g. 'b2b' → themes/b2b/.",
                max_length=64,
            ),
        ),
        migrations.AddField(
            model_name="theme",
            name="is_default",
            field=models.BooleanField(
                default=False,
                help_text=(
                    "Exactly one Theme may be the platform default. "
                    "Enforced by a partial unique index."
                ),
            ),
        ),
        migrations.AddField(
            model_name="theme",
            name="status",
            field=models.CharField(
                choices=[("draft", "Draft"), ("published", "Published")],
                default="draft",
                help_text="Draft themes are not activatable by stores.",
                max_length=20,
            ),
        ),
        migrations.AddField(
            model_name="theme",
            name="thumbnail",
            field=models.ImageField(
                blank=True,
                help_text="Library card image shown in the theme picker.",
                null=True,
                upload_to="themes/thumbnails/",
            ),
        ),
        migrations.AddField(
            model_name="theme",
            name="updated_at",
            field=models.DateTimeField(auto_now=True),
        ),
        migrations.AddConstraint(
            model_name="theme",
            constraint=models.UniqueConstraint(
                condition=models.Q(is_default=True),
                fields=["is_default"],
                name="one_default_theme",
            ),
        ),
        # ------------------------------------------------------------------ #
        # Step 2 — Data migration: seed source_refs + general theme row       #
        # ------------------------------------------------------------------ #
        migrations.RunPython(seed_theme_source_refs, reverse_code=reverse_seed),
        # ------------------------------------------------------------------ #
        # Step 3 — source_ref: remove nullable, add unique constraint         #
        # ------------------------------------------------------------------ #
        migrations.AlterField(
            model_name="theme",
            name="source_ref",
            field=models.CharField(
                help_text="Theme package directory key, e.g. 'b2b' → themes/b2b/.",
                max_length=64,
                unique=True,
            ),
        ),
        # ------------------------------------------------------------------ #
        # Step 4 — Create StoreThemeCustomization                             #
        # ------------------------------------------------------------------ #
        migrations.CreateModel(
            name="StoreThemeCustomization",
            fields=[
                (
                    "id",
                    models.BigAutoField(
                        auto_created=True,
                        primary_key=True,
                        serialize=False,
                        verbose_name="ID",
                    ),
                ),
                (
                    "store",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.PROTECT,
                        to="stores.store",
                    ),
                ),
                (
                    "theme",
                    models.ForeignKey(
                        on_delete=django.db.models.deletion.CASCADE,
                        related_name="customizations",
                        to="stores.theme",
                    ),
                ),
                (
                    "customization_json",
                    models.JSONField(
                        default=dict,
                        help_text=(
                            "Validated by validate_customization() on save. "
                            "Schema: {preset, font_pair, color_overrides, structural}."
                        ),
                    ),
                ),
                (
                    "updated_at",
                    models.DateTimeField(auto_now=True),
                ),
            ],
            options={
                "verbose_name": "store theme customization",
                "verbose_name_plural": "store theme customizations",
            },
        ),
        migrations.AddConstraint(
            model_name="storethemecustomization",
            constraint=models.UniqueConstraint(
                fields=["store", "theme"],
                name="uniq_customization_per_store_theme",
            ),
        ),
    ]
