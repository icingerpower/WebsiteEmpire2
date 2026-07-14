# Generated 2026-07-03 — B6 language dimension + chain-collapse schema

import django.db.models.deletion
from django.db import migrations, models


class Migration(migrations.Migration):
    """
    Drop and recreate Permalink and SlugRedirect with the language dimension.

    Safe to drop+recreate: no production data exists yet (TICKET-016 Phase 1).

    Changes from 0001_initial:
    - Permalink: adds lang, content_type FK, object_id (replaces page_type + target_id),
      auto_created, trigger; unique_together is now (store, lang, slug).
    - SlugRedirect: renames old_slug→from_slug, new_slug→to_slug; adds from_lang, to_lang,
      redirect_type (replaces http_status), is_active, auto_created, trigger; removes
      page_type and target_id; unique_together is now (store, from_lang, from_slug).
    """

    dependencies = [
        ("contenttypes", "0002_remove_content_type_name"),
        ("permalinks", "0001_initial"),
        ("stores", "0003_alter_field_metadata"),
    ]

    operations = [
        # Drop old tables first (no FK references point TO these tables).
        migrations.DeleteModel(name="SlugRedirect"),
        migrations.DeleteModel(name="Permalink"),
        # Recreate Permalink with language dimension and ContentType generic FK.
        migrations.CreateModel(
            name="Permalink",
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
                    "content_type",
                    models.ForeignKey(
                        blank=True,
                        help_text=(
                            "ContentType of the target object.  "
                            "Null for special pages with no target."
                        ),
                        null=True,
                        on_delete=django.db.models.deletion.CASCADE,
                        to="contenttypes.contenttype",
                    ),
                ),
                (
                    "object_id",
                    models.BigIntegerField(
                        blank=True,
                        help_text="PK of the target object.  Null for HOME.",
                        null=True,
                    ),
                ),
                (
                    "lang",
                    models.CharField(
                        default="en",
                        help_text="ISO 639-1 language code — e.g. 'en', 'fr', 'de'.",
                        max_length=10,
                    ),
                ),
                (
                    "slug",
                    models.CharField(
                        db_index=True,
                        help_text=(
                            "Full URL path without leading slash — "
                            "e.g. 'products/my-shirt'."
                        ),
                        max_length=500,
                    ),
                ),
                ("is_active", models.BooleanField(default=True)),
                (
                    "auto_created",
                    models.BooleanField(
                        default=False,
                        help_text="True when created automatically by a signal or import.",
                    ),
                ),
                (
                    "trigger",
                    models.CharField(
                        blank=True,
                        choices=[
                            ("manual", "Manual"),
                            ("slug_change", "Slug change"),
                            ("import", "Import"),
                        ],
                        help_text="What caused this permalink to be created.",
                        max_length=32,
                        null=True,
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
            ],
            options={
                "verbose_name": "permalink",
                "verbose_name_plural": "permalinks",
                "unique_together": {("store", "lang", "slug")},
                "indexes": [
                    models.Index(
                        fields=["store", "lang", "is_active"],
                        name="prmlnk_store_lang_active_idx",
                    )
                ],
            },
        ),
        # Recreate SlugRedirect with language dimension and renamed fields.
        migrations.CreateModel(
            name="SlugRedirect",
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
                    "from_slug",
                    models.CharField(
                        db_index=True,
                        help_text=(
                            "Former URL path that should redirect.  Without leading slash."
                        ),
                        max_length=500,
                    ),
                ),
                (
                    "from_lang",
                    models.CharField(
                        default="en",
                        help_text="Language of the redirected-from slug.",
                        max_length=10,
                    ),
                ),
                (
                    "to_slug",
                    models.CharField(
                        help_text=(
                            "Current URL path to redirect to.  Without leading slash."
                        ),
                        max_length=500,
                    ),
                ),
                (
                    "to_lang",
                    models.CharField(
                        default="en",
                        help_text="Language of the redirect destination slug.",
                        max_length=10,
                    ),
                ),
                (
                    "redirect_type",
                    models.CharField(
                        choices=[
                            ("permanent", "Permanent (301)"),
                            ("temporary", "Temporary (302)"),
                            ("none", "None (dead URL — prevents slug reuse from inheriting old redirect)"),
                        ],
                        default="permanent",
                        help_text="permanent=301, temporary=302, none=intentionally dead URL.",
                        max_length=16,
                    ),
                ),
                ("is_active", models.BooleanField(default=True)),
                (
                    "auto_created",
                    models.BooleanField(
                        default=False,
                        help_text="True when created automatically by a signal or import.",
                    ),
                ),
                (
                    "trigger",
                    models.CharField(
                        blank=True,
                        choices=[
                            ("slug_change", "Slug change"),
                            ("domain_park", "Domain park"),
                            ("import", "Import"),
                        ],
                        help_text="What caused this redirect to be created.",
                        max_length=32,
                        null=True,
                    ),
                ),
                ("created_at", models.DateTimeField(auto_now_add=True)),
            ],
            options={
                "verbose_name": "slug redirect",
                "verbose_name_plural": "slug redirects",
                "unique_together": {("store", "from_lang", "from_slug")},
            },
        ),
    ]
