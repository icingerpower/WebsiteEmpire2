"""
Formset PK isolation fix (ADR-031 addendum, TICKET-051, INLINE-FORMSET-PK-
ISOLATION).

Bug (traced, Django 6.0.4): `BaseModelFormSet.add_fields()`
(forms/models.py:1024-1028) builds the hidden `id` field for an EXISTING row
as `ModelChoiceField(qs)`, where `qs = self.model._default_manager
.get_queryset()` — the raw manager singleton, bypassing every admin-level
`get_queryset()` override entirely (including the `.cross_store_unsafe()`
convention every StoreOwnedModel inline already uses). For a StoreOwnedModel
that manager is the raising `StoreScopedManager` (core/managers.py), so on
POST `ModelChoiceField.to_python()` calls `queryset.get(pk=...)` ->
`__len__` -> `IsolationError`. This blocks EVERY StoreOwnedModel inline POST
(edit an existing row), including catalog.ProductVariantInline, and the same
`add_fields()` code path is reused by `modelformset_factory()` for
`list_editable` changelists (e.g. currency.StoreCurrencySettingAdmin).

The ADR-031 M2M relation-bound relaxation in `core/managers.py` cannot fix
this: that detection keys off `.instance`/`.through` being set on the
*manager instance* used for the read (Django's `ManyRelatedManager`). Here
the manager in play is `self.model._default_manager` — the bare class-level
singleton, with neither attribute set. There is no manager-level signal to
detect "this call originates from a formset's add_fields()"; the fix has to
live in the formset layer.

Fix: swap the pk field's queryset for the formset's OWN `self.queryset`,
which — on both admin paths this ticket covers — is already relation/scope-
safe by construction:
  - BaseInlineFormSet.__init__ filters self.queryset by the parent's FK
    (Django never lets an inline touch another parent's rows).
  - ModelAdmin.changelist_view() builds the changelist formset's queryset
    from ModelAdmin.get_queryset(request) (which every StoreOwnedModel
    ModelAdmin already scopes via .for_store()/.cross_store_unsafe()).

Bonus tightening: pk validation now rejects any pk outside the formset's own
scoped queryset, instead of stock Django's "any row in the whole table" —
closing a minor IDOR probe surface (a store admin could previously submit
another store's/parent's row pk and have Django's ModelChoiceField accept it
as *valid*, even though the row would then never actually render or connect
to the right parent — this made the failure mode a silent no-op instead of
a loud validation error).
"""

from django import forms
from django.forms.models import BaseInlineFormSet, BaseModelFormSet

from core.managers import _RaisingQuerySet
from core.models import StoreOwnedModel


class StoreSafePKFormSetMixin:
    """
    Swaps the hidden pk field's queryset (built by BaseModelFormSet
    .add_fields() from the model's raw `_default_manager`) for the
    formset's own `self.queryset` whenever the stock queryset is a
    `_RaisingQuerySet` over this exact model.

    The `pk_field.queryset.model is self.model` guard deliberately leaves
    the OTHER `add_fields()` branch (forms/models.py ~L1024-1025, taken when
    the model's primary key is itself a ForeignKey/OneToOne — Django builds
    that hidden field's queryset from the *related* model, not `self.model`)
    untouched and still raising: no StoreOwnedModel currently uses an FK/O2O
    primary key (pinned by drift test (c) in
    core/tests/test_formset_pk_isolation_sweep.py), so relaxing that branch
    would be speculative and untested.
    """

    def add_fields(self, form, index):
        super().add_fields(form, index)
        pk_field = form.fields.get(self.model._meta.pk.name)
        if (
            isinstance(pk_field, forms.ModelChoiceField)
            and isinstance(pk_field.queryset, _RaisingQuerySet)
            and pk_field.queryset.model is self.model
        ):
            pk_field.queryset = self.queryset

    def _construct_form(self, i, **kwargs):
        """
        ADR-032 D2: stamp the parent's store onto each inline form instance
        at construction time — the store-site half of the store-excluded
        admin form fix that does not go through
        core.admin.StoreUniqueValidationFormMixin (inline forms are never
        given a _pradize_validation_store; see that mixin's docstring).

        `parent` is only set for BaseInlineFormSet (the formset's own
        `self.instance`, i.e. the parent object being changed/added — a
        Store itself, for the rare case of an inline directly on Store, or
        any other StoreOwnedModel/plain model exposing `.store`).
        Deliberately conditional on `form.instance.store_id is None` so an
        existing row being edited (store already set from the DB) is left
        alone — this is a fresh-instance-only stamp, matching the ordering
        note below.

        Ordering makes this sound on both sites: ModelAdmin._changeform_view
        runs save_form() (-> the parent instance, with store injected by the
        form mixin on the store site, or picked explicitly on the super
        site) BEFORE _create_formsets() constructs the inline formsets, so
        the parent's store exists by inline-validation time even on an add
        page. Extra blank inline forms are unaffected (empty_permitted +
        unchanged -> full_clean() returns early).
        """
        form = super()._construct_form(i, **kwargs)
        parent = getattr(self, "instance", None)  # BaseInlineFormSet only
        if (
            parent is not None
            and issubclass(self.model, StoreOwnedModel)
            and form.instance.store_id is None
        ):
            from stores.models import Store

            store = parent if isinstance(parent, Store) else getattr(parent, "store", None)
            if store is not None and store.pk is not None:
                form.instance.store = store
        return form


class StoreSafeInlineFormSet(StoreSafePKFormSetMixin, BaseInlineFormSet):
    """Drop-in `formset` for any InlineModelAdmin whose model is a StoreOwnedModel."""


class StoreSafeModelFormSet(StoreSafePKFormSetMixin, BaseModelFormSet):
    """
    Drop-in `formset` for `list_editable` changelists on a StoreOwnedModel
    ModelAdmin — inject via `ModelAdmin.get_changelist_formset()`.
    """
