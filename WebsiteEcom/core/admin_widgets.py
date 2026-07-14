"""
Cross-app admin widgets for the Pradize ecommerce engine.

StoreScopedForeignKeyRawIdWidget fixes RAW-ID-WIDGET-ISOLATION (BUG_TESTS.csv):
every ModelAdmin/InlineModelAdmin whose raw_id_fields names a ForeignKey to a
StoreOwnedModel subclass 500s with core.managers.IsolationError the first time
that field re-renders WITH a bound value (any admin validation-error
redisplay, or a change form reached via some paths). Root cause: Django's
stock admin.widgets.ForeignKeyRawIdWidget.label_and_url_for_value() looks up
the currently-selected object via `self.rel.model._default_manager` — for any
StoreOwnedModel that is the raising StoreScopedManager (core/managers.py,
ADR-001 §4), which forbids unscoped queries by design. It is harmless on a
blank add form (no lookup happens), but the instant a bound value is present
the widget crashes the whole page — turning an unrelated form error into a
500 that masks the real validation message the user was supposed to see.

First identified in catalog/admin.py while writing the PV-SLUG-COLLISION-500
regression test (ProductPageVersionAdmin.raw_id_fields = ["product"]);
promoted here during the RAW-ID-WIDGET-ISOLATION sweep once the same pattern
turned up in campaigns/, aijobs/, and emails/ admins too — it is a cross-app
concern, not a catalog one.

Why relaxing the READ is safe (ADR-031's "relax reads, guard writes"
precedent, the same one used for relation-bound M2M reads in
core/managers.py): this widget only ever renders a truncated label + edit
link for a PK that the ModelForm's own formfield_for_foreignkey queryset
(scoped to request.store, or otherwise deliberately restricted) already
validates independently. label_and_url_for_value() never gates access — it
only decorates whatever value was already submitted/selected for display, so
using .cross_store_unsafe() here cannot leak a cross-store write; it can only
ever affect what label is shown for a PK the write path already accepted or
rejected on its own terms.

Usage — in a ModelAdmin/InlineModelAdmin.formfield_for_foreignkey, for any
db_field in raw_id_fields whose target is a StoreOwnedModel::

    def formfield_for_foreignkey(self, db_field, request, **kwargs):
        if db_field.name == "product":
            kwargs["widget"] = StoreScopedForeignKeyRawIdWidget(
                db_field.remote_field, self.admin_site, using=kwargs.get("using"),
            )
        return super().formfield_for_foreignkey(db_field, request, **kwargs)

Do NOT use this to change queryset scoping for the selection popup or for
form validation — that is a separate concern (formfield_for_foreignkey's
`kwargs["queryset"]`) and must stay whatever it already is for that admin.
"""

from django.contrib import admin


class StoreScopedForeignKeyRawIdWidget(admin.widgets.ForeignKeyRawIdWidget):
    """
    ForeignKeyRawIdWidget for an FK to a StoreOwnedModel.

    Overrides label_and_url_for_value() to resolve the selected object via
    .cross_store_unsafe() instead of the model's raising default manager —
    see the module docstring for the full bug class and why this read is
    safe. This class never gates access; it only decorates a PK value that
    the surrounding form's queryset already validated (or rejected) on its
    own terms.
    """

    def label_and_url_for_value(self, value):
        from django.core.exceptions import ValidationError as DjangoValidationError
        from django.urls import NoReverseMatch, reverse
        from django.utils.text import Truncator

        key = self.rel.get_related_field().name
        try:
            obj = self.rel.model.objects.cross_store_unsafe().get(**{key: value})
        except (ValueError, self.rel.model.DoesNotExist, DjangoValidationError):
            return "", ""

        try:
            url = reverse(
                f"{self.admin_site.name}:{obj._meta.app_label}_{obj._meta.model_name}_change",
                args=(obj.pk,),
            )
        except NoReverseMatch:
            url = ""

        return Truncator(obj).words(14), url
