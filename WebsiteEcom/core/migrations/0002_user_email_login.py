"""
Migration 0002: Make User.email unique and designate it as the login field.

EMAIL-LOGIN (T001, T002 Spec Reviewer MAJOR finding):
- email is now USERNAME_FIELD; authentication lookups use email, not username.
- unique=True is enforced at the DB level so two accounts cannot share an email.
- blank=True is retained (one empty-string email is permitted) to avoid a data
  migration on dev databases that have accounts without an email address.
- username is not changed — it remains a required field on the model.

This migration is safe to apply to existing databases: AlterField on a non-indexed
EmailField adds a UNIQUE index.  If any two existing rows share the same email
(e.g. both empty), the migration will fail — fix data before applying.
"""

from django.db import migrations, models


class Migration(migrations.Migration):

    dependencies = [
        ("core", "0001_initial"),
    ]

    operations = [
        migrations.AlterField(
            model_name="user",
            name="email",
            field=models.EmailField(
                blank=True,
                max_length=254,
                unique=True,
                verbose_name="email address",
            ),
        ),
    ]
