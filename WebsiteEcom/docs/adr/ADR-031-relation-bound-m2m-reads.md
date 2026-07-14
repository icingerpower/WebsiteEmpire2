# ADR-031: Relation-bound M2M access on StoreOwnedModel — relax the read raise, guard the write

**Date:** 2026-07-11 · **Status:** ACCEPTED · **Amends:** ADR-001 §4 (`core/managers.py`, `StoreScopedManager`)
**Trigger:** BUG_TESTS.csv `DISCOUNTS-INLINE-ISOLATION` note + ADR-029 addendum 2026-07-11.

## Decision

1. `StoreScopedManager.get_queryset()` returns a **real** queryset instead of `_RaisingQuerySet` **only** when `self` is an M2M relation-bound manager — detectable because Django's `ManyRelatedManager` subclasses the target model's `_default_manager.__class__` and sets **both** `self.instance` and `self.through` in `__init__`. All other access (direct `Model.objects.*`, reverse-FK related managers, which set `instance` but not `through`) keeps raising `IsolationError` exactly as today.
2. A new **write-side guard** in `core/` connects to `m2m_changed` (`pre_add`) for **every** M2M field whose source and target are both `StoreOwnedModel` subclasses, and raises `IsolationError` if any added target row's `store_id` differs from `instance.store_id`.
3. The per-call-site write workarounds (`GiftCardCampaignAdmin.save_related` through-table bypass; the manager-avoidance patterns documented in `engagement/service.py`) become unnecessary; the `save_related` override is removed.

**Invariant:** *Cross-store M2M rows are prevented at write time by the core `m2m_changed` pre_add guard (plus store-scoped form-field querysets as first line); relation-bound M2M reads are therefore store-safe by construction, because `_apply_rel_filters` pins every row to the through table of one already-scoped instance, and every through row links two same-store objects.*

## Context

Every M2M targeting a `StoreOwnedModel` crashes both form paths, because Django 6.0.4 builds the related manager from the **target's `_default_manager` class** (`related_descriptors.py:995-1002`), i.e. from `StoreScopedManager`:

- **Read (newly found, blocks change views today):** `ModelForm(instance=obj)` → `model_to_dict()` → `ManyToManyField.value_from_object()` → `list(getattr(obj, attname).all())` (`forms/models.py:113-120`, `fields/related.py:2039-2040`) → `ManyRelatedManager.get_queryset()` → `super().get_queryset()` → `_RaisingQuerySet` → `IsolationError`. Verified broken: `DiscountCode` (product_conditions/collection_conditions) and `GiftCardCampaign` (trigger_products) change views. Same class of trap: `LeadCaptureCampaign.excluded_pages`, `FeedConfig.collections` — all five M2M-onto-StoreOwnedModel fields in the codebase, all admin-editable.
- **Write (known):** `form.save_m2m()` → `.set()` reads current state via `self.using(db).values_list(...)` through the same manager — worked around per-site three times already (§XV-1's invisible-failure pattern: every new site forgets).

## Options considered

- **A. `Meta.base_manager_name` → plain manager.** Rejected on traced source: `_base_manager` is consulted only by **forward FK** descriptors (`related_descriptors.py:170,449`) and internal cascades. The M2M descriptor uses `_default_manager.__class__` (line 999), so A does not fix `model_to_dict` at all. The variant `default_manager_name` → plain manager *would* fix it, but silently unscopes every `_default_manager` consumer (default `ModelAdmin.get_queryset`, dumpdata, third-party code) — precisely the silent bleed ADR-001 §4 exists to prevent.
- **B. Relax the raise for relation-bound M2M access + write-side store-consistency guard. CHOSEN.**
- **C. ModelForm mixin overriding M2M initial loading.** Rejected: opt-in at every future form (three sites already bitten by the write twin), and it leaves `save_m2m()` broken, so the per-site write workarounds live forever.

## Why B

A relation-bound read cannot bleed across stores **iff** through rows never cross stores. Today nothing enforces that — `GiftCardCampaignAdmin.formfield_for_manytomany` even offers `Product.objects.cross_store_unsafe()` in the widget, so a super-admin can select another store's product; only the accidental read crash "protected" us. The guard converts that hazard into a loud, tested invariant, after which the read relaxation is provably safe and *all* current and future M2M forms work with stock Django machinery — no per-site memory required (§XV-1).

## Implementation sketch

**`core/managers.py`:**

```python
def get_queryset(self):
    # M2M relation-bound access: ManyRelatedManager subclasses this class and
    # sets BOTH .instance and .through (Django 6.0 related_descriptors.py).
    # Safe by construction: _apply_rel_filters pins rows to this instance's
    # through rows, and core/m2m_guard.py guarantees through rows never cross
    # stores (ADR-031). Reverse-FK managers (instance, no through) still raise.
    if getattr(self, "through", None) is not None and getattr(self, "instance", None) is not None:
        return models.QuerySet(self.model, using=self._db, hints=self._hints)
    return _RaisingQuerySet(self.model, using=self._db, hints=self._hints)
```

**`core/m2m_guard.py` (new), connected from `CoreConfig.ready()`:**

```python
def _guard(sender, instance, action, reverse, model, pk_set, **kwargs):
    if action != "pre_add" or not pk_set:
        return
    if model.objects.cross_store_unsafe().filter(pk__in=pk_set).exclude(
            store_id=instance.store_id).exists():
        raise IsolationError(
            "Cross-store M2M link forbidden (ADR-031): %s → %s"
            % (instance._meta.label, model._meta.label))

def connect_store_m2m_guards():
    for m in apps.get_models():
        if not issubclass(m, StoreOwnedModel):
            continue
        for f in m._meta.local_many_to_many:
            if issubclass(f.remote_field.model, StoreOwnedModel):
                m2m_changed.connect(_guard, sender=f.remote_field.through,
                                    dispatch_uid=f"store_m2m_guard.{m._meta.label_lower}.{f.name}")
```

**Cleanups in the same ticket (TICKET-050):** remove `GiftCardCampaignAdmin.save_related`; scope `GiftCardCampaignAdmin.formfield_for_manytomany`'s trigger_products widget to the campaign's store on change (keep `cross_store_unsafe()` only where no instance store exists yet, and rely on the guard); fix `DiscountCodeAdmin.get_form`'s `Product.objects.filter(store=request.store)` (chains off the raising queryset — must be `.for_store(request.store)`); `engagement/service.py` comments referencing the limitation may be simplified but its multi-language path resolution stays.

## Risks

- Relaxation also legalises reverse-M2M access (`product.discount_codes`) and M2M `prefetch_related` — both relation-bound, covered by the same invariant.
- Direct writes to auto-created through models bypass signals (`through.objects.bulk_create`) — through models are not exposed anywhere after `save_related` is removed; grep-auditable.
- Mixed M2M (only one end store-owned) gets no guard — none exist; the compliance test below forces an ADR the day one appears.
- One extra `EXISTS` query per M2M add — negligible (admin/service writes only).

## Rollback strategy

Pure code change, no migration: revert the `core/managers.py` branch and the `ready()` hook, restore the removed `save_related` override (single commit revert). Change views return to broken-as-today, nothing worse.

## Tests required

1. **Blocked views render over full HTTP** (client GET, 200, widget choices rendered): DiscountCode change (store + super admin), GiftCardCampaign change (super admin), LeadCaptureCampaign change, FeedConfig change — each with existing M2M rows. After-bug proof cycle: revert manager change → these fail with `IsolationError`.
2. **Stock write path works:** admin POST persisting M2M selections via plain `form.save_m2m()` (with `save_related` override removed), including removing previously-selected rows.
3. **Cross-store M2M is impossible:** `.add()` / `.set()` with another store's object raises `IsolationError` (no through row written); admin POST with a forged cross-store pk yields a validation error or 500, never a stored row; one service-layer path (`campaigns` reward or `engagement`) equally guarded.
4. **Isolation stays loud:** `Model.objects.all()` / `.filter()` evaluation still raises; a reverse-FK accessor on a StoreOwnedModel still raises (status-quo pin).
5. **Compliance:** introspection test asserting every M2M between StoreOwnedModels has a connected guard (`dispatch_uid`), and that no M2M targets a StoreOwnedModel from a non-store-owned model (extend `core/tests/test_store_owned_model_compliance.py`).

---

## Addendum (2026-07-11, Architect) — two sibling holes: formset PK lookups, and non-raising terminal methods

**Status:** ACCEPTED · **Trigger:** raw_id_fields sweep + BUG_TESTS.csv `CAMPAIGN-STEP-INLINE-ISOLATION` note. Both P0, ticketed as TICKET-051.

### Hole 1 — INLINE-FORMSET-PK-ISOLATION (blocks every StoreOwnedModel inline POST, incl. ProductVariantInline)

**Context (traced, Django 6.0.4):** `BaseModelFormSet.add_fields()` builds the hidden `id` field for existing rows as `ModelChoiceField(qs)` where `qs = self.model._default_manager.get_queryset()` (`forms/models.py:1024-1028`) — the raw manager **singleton**, bypassing every admin `get_queryset()` override. On POST, `ModelChoiceField.to_python()` runs `queryset.get(pk=...)` → `__len__` → `IsolationError`. **The ADR-031 relation-bound detection is structurally blind here**: the manager carries no `instance`/`through` attributes (those exist only on `ManyRelatedManager` instances); there is no signal at the manager level that this call is formset-bound. A manager-level fix is therefore impossible — the sweep agent's formset-layer fix is correct. Same mechanism hits a **third surface**: `list_editable` changelists (`modelformset_factory` → same `add_fields`) — live today on `currency.StoreCurrencySettingAdmin` (store-scoped, `list_editable = ["is_enabled"]`), whose bulk-save POST 500s identically.

**Decision:** shared formset mixin swapping the pk field's queryset for the formset's own — which Django already provides scoped on both admin paths:

```python
# core/formsets.py
class StoreSafePKFormSetMixin:
    """add_fields() (forms/models.py L1024-28) builds the hidden pk field's
    queryset from the raw _default_manager — a _RaisingQuerySet for
    StoreOwnedModels. Swap it for self.queryset, which is relation-bound by
    construction on both paths: BaseInlineFormSet.__init__ parent-FK-filters
    it; changelist_view passes ModelAdmin.get_queryset(request)-derived rows.
    Bonus: pk validation now rejects pks outside the scoped set (tighter than
    stock Django, which validated against ALL rows — an IDOR probe surface)."""
    def add_fields(self, form, index):
        super().add_fields(form, index)
        pk_field = form.fields.get(self.model._meta.pk.name)
        if (isinstance(pk_field, forms.ModelChoiceField)
                and isinstance(pk_field.queryset, _RaisingQuerySet)
                and pk_field.queryset.model is self.model):
            pk_field.queryset = self.queryset

class StoreSafeInlineFormSet(StoreSafePKFormSetMixin, BaseInlineFormSet): pass
class StoreSafeModelFormSet(StoreSafePKFormSetMixin, BaseModelFormSet): pass
```

Delivery — stop relying on per-site memory (§XV-1; this family has now bitten five times): new `core/admin.py` base `StoreOwnedInlineMixin` (`formset = StoreSafeInlineFormSet` + the established `get_queryset() → cross_store_unsafe()` convention, today copy-pasted across orders/catalog/discounts/campaigns inlines); migrate all 21 inlines; `campaigns._FormsetWithDefaultPosition` re-parents onto `StoreSafeInlineFormSet`. Admins with `list_editable` on a StoreOwnedModel override `get_changelist_formset(request, **kwargs)` to inject `kwargs.setdefault("formset", StoreSafeModelFormSet)`.

**Drift tests (introspective, mirror `core/tests/test_raw_id_widget_sweep.py::_iter_admins_and_inlines`):** (a) every registered inline (both sites) whose model is a StoreOwnedModel has `issubclass(inline.formset, StoreSafeInlineFormSet)`; (b) every admin with `list_editable` on a StoreOwnedModel yields `issubclass(admin.get_changelist_formset(request), StoreSafeModelFormSet)`; (c) no StoreOwnedModel has a ForeignKey/OneToOne primary key (pins the *other* `add_fields` branch, L1024-25, which the mixin's `model is self.model` check deliberately leaves raising — verified none exist today).

### Hole 2 — AGGREGATE-DOESNT-RAISE (and its full family): terminal methods bypassing the raise

**Context (each body read in Django 6.0.4 `db/models/query.py`):** `_RaisingQuerySet` overrides only `__iter__`/`__len__`/`__bool__`/`_fetch_all`. Silent bypasses — each executes SQL directly: `aggregate()` (→ `query.get_aggregation`, L594), `count()` (→ `query.get_count`, L608), `exists()` (→ `query.has_results`, L1337) and via it `contains()`, `iterator()` (→ `self._iterator`, L533) and `aiterator()` (builds `_iterable_class` directly, **not** via `iterator()`), `update()` (compiler `execute_sql` — a **silent unscoped cross-store WRITE**, strictly worse than the read gaps), `bulk_update()` (covered transitively by `update()`, pinned anyway), fast-path `delete()` (`Collector.can_fast_delete` skips the fetch), `explain()`. Already-raising, pinned by status-quo tests: `get()` (via `__len__`), `first()`/`last()` (iterate a sliced clone), `in_bulk`, `earliest`/`latest`, `values`/`values_list` evaluation; all other async variants are `sync_to_async` wrappers of their sync counterparts. **The gap is load-bearing today:** the sweep found one silent unscoped aggregate (`CampaignStep...aggregate(Max("position"))`, fixed narrowly), and `discounts/service.py:194,690,703` run the §XIII atomic coupon decrements as `DiscountCode.objects.filter(pk=...).update(...)` — working only *because* of the hole.

**Decision:** make every listed method raise; add one sanctioned safe entry point, `StoreScopedManager.none()` (returns a real empty queryset — "empty by construction" leaks nothing; today `Model.objects.none()` returns a raising clone, a booby trap already planted in `DiscountCodeAdmin.get_queryset` and `StoreCurrencySettingAdmin.get_queryset` no-store paths).

```python
# core/managers.py — _RaisingQuerySet additions (all bodies: raise IsolationError(self._UNSCOPED_MESSAGE))
aggregate / count / exists / contains / iterator / aiterator / update / bulk_update / delete / explain

# StoreScopedManager addition
def none(self):
    return models.QuerySet(self.model, using=self._db).none()
```

**Mandatory migration in the same ticket:** audit every `<StoreOwnedModel>.objects.filter(...)` chain ending in a newly-raising terminal (~70 `.objects.filter(` sites repo-wide, only StoreOwnedModel targets in scope; `stores.StoreLanguage`/`ShippingCountry`/`StoreDomain` are plain models — exempt). Known conversions: the three `discounts/service.py` atomic updates become `DiscountCode.objects.for_store(code.store).filter(pk=code.pk).update(...)` (store is in hand at every site; **no** new unsafe entry point). Every other hit is either converted to `.for_store()`/`.cross_store_unsafe()` or is a genuine latent cross-store bug to fix — surfacing those is the point of ADR-001 §4.

### Options considered (both holes)

Extending the ADR-031 relation-bound relaxation to cover hole 1 — **rejected on traced source** (no manager-level signal exists; see above). Per-call-site fixes — rejected, fifth recurrence of the family. For hole 2, raising only `aggregate()` — rejected: `update()` is the worst member (silent cross-store write), and leaving any sibling open re-creates this addendum next sweep.

### Risks

- The terminal-method closure **will** surface latent unscoped queries as new `IsolationError`s (by design). Mitigation: the audit above + full suite green before merge; each surfaced site is a pre-existing correctness bug, not a regression.
- `pk_field.queryset = self.queryset` changes pk-validation semantics from "any row in the table" to "any row in the scoped set" — strictly tighter; a legitimate row can never be outside the scoped set (inline rows are parent-FK rows; changelist rows come from the admin's own queryset).
- `contains()` is covered via `exists()` on a preserved-class clone — pinned by an explicit test, not assumed.

### Rollback strategy

Pure code, no migrations: one commit revert restores today's behavior (broken inline POSTs, silent aggregates). The `discounts/service.py` conversions are behavior-identical (same SQL, added store predicate on pk-pinned rows) and may stay through a rollback.

### Tests required (TICKET-051)

1. **Inline POST over full HTTP:** edit an existing ProductVariant via ProductVariantInline (store admin) and an existing row on one super-admin inline → 200/302, row updated. Proof cycle: revert the formset mixin → `IsolationError`.
2. **Changelist editable POST:** `StoreCurrencySettingAdmin` bulk-save toggles `is_enabled` → success; revert → `IsolationError`.
3. **PK-tamper rejection:** inline POST with a hidden-id pk from another store (and from another parent, same store) → clean validation error, no write, no 500.
4. **Terminal-method matrix:** parametrized test over `aggregate/count/exists/contains/iterator/aiterator/update/bulk_update/delete/explain` on `Model.objects.filter(...)` → each raises `IsolationError`; status-quo pins for `get/first/last/in_bulk/values_list` evaluation; `Model.objects.none()` evaluates to `[]` without raising.
5. **Atomic decrement regression:** existing §XIII coupon decrement tests stay green after the `for_store` conversion (same-transaction, pk-pinned).
6. **Drift tests** (a)-(c) from Hole 1.

---

## Addendum 3 (2026-07-11, Architect) — the validation ring: model validation and default choice widgets ran through the silent-exists hole

**Status:** ACCEPTED · **Trigger:** TICKET-051's terminal-method closure turned the suite red (27/28 failures, one root cause) — BUG_TESTS.csv `TERMINAL-METHODS-ISOLATION` follow-up. Ticketed as TICKET-052 (P0, tree red).

### Context (traced, Django 6.0.4 — every line verified)

Model validation was never isolation-safe; it **worked for months only because `exists()` silently bypassed the raise**. Four internal paths query through `Model._default_manager` (NOT `_base_manager` — decisive, see below) and terminate in the newly-raising `exists()`/`count()`:

1. `Model._perform_unique_checks` — `model_class._default_manager.filter(**lookup_kwargs)` (`db/models/base.py:1538`) → `qs.exists()`. Breaks `full_clean()`/admin validation for every `unique_together`/`unique=True` on a StoreOwnedModel.
2. `Model._perform_date_checks` — same shape (`base.py:1579`); no `unique_for_*` exists on any StoreOwnedModel today, covered anyway by the same window.
3. `UniqueConstraint.validate` — `model._default_manager.using(using)` (`constraints.py:573`) → filtered → `exists()`. **No queryset-injection hook exists** in its signature (`validate(self, model, instance, exclude, using)`). `CheckConstraint.validate` (`constraints.py:210-218`) evaluates the instance's field-expression map via `Q.check` — no manager, unaffected; the CheckConstraint-bearing models fail via their unique paths inside the same `full_clean()`.
4. `ModelChoiceIterator.__len__` → `queryset.count()` and `__bool__` → `queryset.exists()` (`forms/models.py:1455,1458`) over the stock FK/M2M formfield queryset, which is the raw `_default_manager.using(using)` (`related.py:1208,2048`).

**`Meta.base_manager_name` is doubly irrelevant:** the four paths use `_default_manager`, and `_base_manager` is *already* an auto-created plain `Manager` when `base_manager_name` is unset (`options.py:471-495` falls through to `Manager()` — which is also why forward FK dereference has always worked).

**Constraint enumeration (per request):** every unique constraint on the affected models either names `store` directly (all `unique_together`s across catalog/pages/permalinks/engagement/pixels/orders; catalog's `(Upper('sku'), store)` UniqueConstraint; engagement's `unique_social_proof_settings_per_store`) or pins it transitively through store-owned FKs (`catalog.CollectionProduct ('collection','product')`). **No store-independent (global) constraint exists today** — but the chosen fix preserves stock Django semantics, so a future global constraint (e.g. a platform-unique gift-card code) validates correctly cross-store without revisiting this ADR.

### Decision

**D1 — model validation: a contextvar validation window that unlocks ONLY `exists()`.**

```python
# core/managers.py
_MODEL_VALIDATION_WINDOW = contextvars.ContextVar("store_model_validation", default=False)

class _RaisingQuerySet(models.QuerySet):
    def exists(self):
        # Django's own validation lookups (base.py:1538/1579, constraints.py:573)
        # filter by exactly the constraint's fields before calling exists(); every
        # current constraint includes/pins store, and a global constraint SHOULD
        # check cross-store. Stock semantics, sanctioned entry.
        if _MODEL_VALIDATION_WINDOW.get():
            return super().exists()
        raise IsolationError(self._UNSCOPED_MESSAGE)

# core/models.py — StoreOwnedModel
def validate_unique(self, exclude=None):
    token = _MODEL_VALIDATION_WINDOW.set(True)
    try:
        return super().validate_unique(exclude)
    finally:
        _MODEL_VALIDATION_WINDOW.reset(token)

def validate_constraints(self, exclude=None):   # identical wrapper
```

Properties: wraps only the two Django internals — `full_clean()` runs user `clean()`/`clean_fields()` **outside** the window, so application code keeps loud isolation; only `exists()` is unlocked — `update/delete/aggregate/count/iteration` keep raising even mid-validation; try/finally is exception-safe; `contextvars` is thread- and async-safe.

**Options rejected:** copying `_perform_unique_checks` into StoreOwnedModel (~55 lines of version-fragile internals, and it cannot reach `UniqueConstraint.validate`'s embedded manager call — that would additionally need a `UniqueConstraint` subclass whose changed `deconstruct()` path forces migration churn across every constrained model); unconditionally relaxing `exists()` (leaks cross-store booleans, guts §XV-1); `Meta.base_manager_name` (traced irrelevant, above).

**D2 — default choice widgets: scope at the admin layer, same family as the raw-id fix.** Extend `core/admin.py` with `StoreScopedFormFieldsMixin` providing shared `formfield_for_foreignkey`/`formfield_for_manytomany`: when the target is a StoreOwnedModel and no queryset was supplied — store site → `.for_store(request.store)`; super site → `.cross_store_unsafe()` (narrow to `obj.store` where an instance is bound, per the TICKET-050 GiftCardCampaign precedent). Mixed into `StoreOwnedInlineMixin` and the admin base classes. This is scoping-by-construction, not relaxation — the widget queryset SHOULD be store-scoped; letting the raw `_default_manager` default through was always wrong, merely masked. Non-admin ModelForms remain loud by design (convention: views pass scoped querysets).

**D3 — folded dev items (no architecture, agreed):** `CsvListWidget`×`JSONField.bound_data` crash and the pre-existing feeds `RegenerateActionTest` failure are ordinary implementation fixes inside TICKET-052.

### Risks

- The window is ambient state; kept safe by its narrowness (two wrapped Django entry points, one unlocked method). A subclass overriding `validate_unique` runs its own pre/super/post code outside the window except the `super()` call itself.
- D2 changes super-admin select widgets from raise-on-render to cross-store lists — intended (matches every existing super-admin convention).
- Upgrade drift: the four traced line numbers are 6.0.4-specific; the validation-window tests (below) fail loudly if a future Django routes validation differently.

### Rollback strategy

Pure code, no migrations, one commit revert. Reverting re-reds the suite (validation raises again) but loses no data; D1/D2 are independently revertable.

### Tests required (TICKET-052)

1. **Validation ring green:** the 27 red tests pass; plus explicit full-HTTP admin add/change POSTs that trip a `unique_together` violation (e.g. duplicate `pages.StaticPage` slug in-store) → clean form error, not 500; and a `UniqueConstraint` violation (engagement.SocialProofSettings second row per store) → clean error.
2. **Window is narrow:** inside a model whose `clean()` performs an unscoped `Model.objects.filter(...).exists()`, `full_clean()` still raises `IsolationError` (user code not sanctioned); `update()`/`count()` on a raising queryset raise even when called during `validate_unique` (probe via a test-only model/monkeypatch).
3. **Cross-store duplicates still allowed / in-store rejected:** same slug on two stores validates; same slug same store fails — proving the window didn't change constraint semantics.
4. **Widget scoping drift test (introspective, both sites):** for every registered ModelAdmin/inline, build the form/formset with a request and assert no form-field queryset is a `_RaisingQuerySet` — catches select, raw-id, and future autocomplete variants in one sweep; store-site FK widgets contain only `request.store` rows.
5. **D3 regressions:** CsvListWidget round-trip over a bound JSONField form; feeds RegenerateAction test green.
6. **Proof cycle:** revert D1 wrappers → group-1 tests fail with `IsolationError`; revert D2 mixin → group-4 drift test fails.
