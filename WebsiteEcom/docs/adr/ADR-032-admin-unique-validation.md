# ADR-032: Admin unique validation — store-excluded forms must inject the store and un-exclude it before validate_unique

**Date:** 2026-07-11 · **Status:** ACCEPTED (Architect adjudication per TICKET-052 verification directive; no product-level choice involved — a 500 becomes a form error, behavior all specs already assume) · **Complements:** ADR-031 Addendum 3 (D1 validation window), ADR-001 §4
**Trigger:** BUG_TESTS.csv `VALIDATION-WINDOW-ISOLATION` "separate finding for the Architect Agent" + TICKET-052 code comments (direct reproduction on `pages.StaticPage`).

## Decision

1. **D1 — one shared form mixin, two primitives.** `core/admin.py` gains `StoreUniqueValidationFormMixin`, a ModelForm mixin that (a) injects `instance.store` from a per-request class attribute **before** `BaseModelForm._post_clean()` runs, and (b) overrides `_get_validation_exclusions()` to `discard("store")` whenever `store` is not a form field and `instance.store_id` is already set. Both primitives are required (see Context — there are two independent kill switches).
2. **D2 — wired by construction, never per-admin.** The wrapping happens inside the machinery that already reaches every admin: `StoreScopedFormFieldsMixin.get_form()` (auto-mixed into every ModelAdmin on both sites by `_StoreScopedRegistrationMixin.register()`, webecom/admin.py) wraps top-level form classes; `StoreOwnedInlineMixin.get_formset()` wraps inline form classes; `StoreSafeInlineFormSet` (core/formsets.py) copies the **parent's** store onto each inline form instance at construction time. No concrete admin is edited.
3. **D3 — super-site structural rule.** On `super_admin_site` there is no `request.store` truth to inject, so injection is store-site-only. Instead: any **add-capable** super-site ModelAdmin over a StoreOwnedModel with a store-inclusive constraint must expose `store` as a real form field (the existing `DiscountCodeSuperAdmin` convention — a super admin explicitly picks the store, and stock Django then validates the tuple natively). Enforced by the drift test, not by code. Change forms on the super site get friendly validation for free from D1(b) (the instance's `store_id` comes from the DB).
4. **D4 — no global IntegrityError catcher.** Option (c) is rejected as a substitute and not built as a complement either (see Options). The post-validation TOCTOU race window remains and a 500 under a true concurrent duplicate is accepted — identical to stock Django's posture on every unique constraint everywhere.
5. **D5 — CsvListWidget display/corruption fix (ordinary dev item, no architecture).** Three parts, specified under Implementation sketch §D5: `CsvListFormField.prepare_value()` list passthrough; `CsvListFormField.to_python()` empty→`[]`; `blank=True` on `catalog.Product.tags` and `stores.Organization.coverage_areas_json` + migrations. Folded into TICKET-053.

## Context

Every store-admin-site ModelAdmin follows the established convention: `store` is excluded from the form and assigned in `save_model(request, obj, form, change)` (e.g. `pages/admin.py` StaticPageAdmin:133). Traced against Django 6.0.4, that convention kills friendly unique validation through **two independent mechanisms**, and the ordering guarantees neither can self-heal:

- **Ordering (the root):** `ModelAdmin._changeform_view` calls `form.is_valid()` at contrib/admin/options.py:1847 and `save_model()` at :1853. Validation is over before `save_model` ever assigns `obj.store`. During `_post_clean()` the instance has `store_id = None`.
- **Kill switch 1 — exclusion:** `BaseModelForm._get_validation_exclusions()` (forms/models.py:396) adds every model field "not in self.fields" to `exclude` — `store` always qualifies. `Model._get_unique_checks()` (db/models/base.py:1443) then drops any `unique_together` tuple with `any(name in exclude for name in check)` (:1470-1479), and `UniqueConstraint.validate()` (db/models/constraints.py:572) hard-`return`s on `if exclude and field_name in exclude` (:579). The whole `(store, slug)` check vanishes.
- **Kill switch 2 — the None skip:** even with the exclusion removed, `Model._perform_unique_checks()` (base.py:1511) skips any lookup whose value is None (`lookup_value is None → continue`, then `len(unique_check) != len(lookup_kwargs) → continue`, :1527-1537), and `UniqueConstraint.validate()` `return`s on a None component ("NULL != NULL in SQL"). With `store_id = None` at validation time, the check silently passes.
- Result: a same-store duplicate submitted through the real admin HTTP endpoint sails through `form.is_valid()`, and `save_model` → `INSERT` 500s with a raw `IntegrityError`. Confirmed by direct reproduction on `pages.StaticPage` during TICKET-052 (which is why that ticket's HTTP regression had to use `DiscountCodeSuperAdmin`, whose fieldsets DO include `store`).
- **What already works and must not regress:** ADR-031 Addendum 3 D1 (the contextvar validation window on `StoreOwnedModel.validate_unique`/`validate_constraints`) is proven correct — wherever `store` IS in the form, the full chain `_post_clean → validate_unique → _perform_unique_checks → exists()` produces a clean form error. This ADR only makes the store-excluded forms reach that same, already-proven chain.
- `_post_clean` internals that constrain the fix (forms/models.py:479-513): it computes `exclude`, runs `construct_instance()` (which never touches `store` — not a form field), runs `instance.full_clean(exclude=exclude, validate_unique=False, validate_constraints=False)`, then calls `form.validate_unique()` and `form.validate_constraints()`, **each of which recomputes** `_get_validation_exclusions()` (:520, :531). So overriding `_get_validation_exclusions()` on the form covers all three consumers at once, and injecting the store before `super()._post_clean()` covers every downstream read.

## Options considered

- **(a) Shared form mixin: inject `instance.store` before `_post_clean` + remove `store` from the exclusions. CHOSEN.** The standard Django recipe for "field set outside the form", applied once at the registration layer. Reaches the proven ADR-031 window; converts add AND change duplicates (single POST cross-form inline duplicates too, since `BaseModelFormSet.validate_unique` at forms/models.py:824 also consumes `form._get_validation_exclusions()` at :835) into ordinary field/non-field errors.
- **(b) Per-admin `clean()` duplicate checks. REJECTED.** Per-site opt-in is precisely the failure family §XV-1 names and this codebase has been bitten by five-plus times (ADR-031's addenda catalogue them): every future admin forgets, and the check duplicates what Django already does correctly once the two kill switches are removed. It would also hand-reimplement `qs.exclude(pk=…)` edit semantics per admin.
- **(c) Catch IntegrityError globally, re-render with a form error. REJECTED as substitute; not built as complement.** Honest assessment: it is the only mechanism that can catch the true concurrent race, but it is lossy (a DB driver error string cannot be reliably mapped back to a field, so the user gets a vague non-field error), backend-dependent, and — worse — it would silently absorb *unrelated* IntegrityErrors (FK violations, NOT NULL bugs) into "please try again" messages, converting loud defects into invisible failures (§XV-1). The race it uniquely covers is the same race stock Django accepts on every unique field in every admin; nothing in our funnel makes admin CRUD concurrency-hot. If ops data ever shows real-world race 500s, revisit as a narrow, constraint-name-matching complement.

## Why (a)

It removes exactly the two traced kill switches and nothing else; every downstream behavior (unique_together tuple assembly, UniqueConstraint validation including expression constraints like ProductVariant's `Upper('sku'), store`, the edit-case `exclude(pk=…)`, the ADR-031 isolation window, formset cross-form dedup) is stock Django, already exercised and already proven where `store` is in the form. Registration-layer wiring means the fix cannot be forgotten by the next admin — the same "enforcement by construction" argument that carried ADR-031 D2, `StoreOwnedInlineMixin`, and `StoreSafePKFormSetMixin`.

## Implementation sketch

**`core/admin.py` — the mixin (D1):**

```python
class StoreUniqueValidationFormMixin:
    """
    ADR-032: store-excluded admin forms must still validate store-inclusive
    unique constraints. Two primitives, both required (two independent kill
    switches — see the ADR):
      1. inject instance.store BEFORE BaseModelForm._post_clean() runs, so
         Model._perform_unique_checks / UniqueConstraint.validate stop
         None-skipping the whole check;
      2. discard "store" from _get_validation_exclusions(), so the check is
         assembled at all (checked tuple-wise in Model._get_unique_checks and
         field-wise in UniqueConstraint.validate).
    _pradize_validation_store is set on a per-request form class (ModelAdmin
    .get_form / inlineformset_factory build a fresh class per call, so a class
    attribute cannot leak across requests). None on the super site and for
    inline forms — inline injection comes from the parent instance via
    StoreSafeInlineFormSet instead.
    """

    _pradize_validation_store = None

    def _post_clean(self):
        if (
            self._pradize_validation_store is not None
            and self.instance.store_id is None
        ):
            self.instance.store = self._pradize_validation_store
        super()._post_clean()

    def _get_validation_exclusions(self):
        exclude = super()._get_validation_exclusions()
        if "store" not in self.fields and self.instance.store_id is not None:
            exclude.discard("store")
        return exclude
```

Notes pinned by tests: injection is a no-op when `store_id` is already set (change forms, super-site forms with a store field); `save_model`'s existing `obj.store = request.store` stays — it becomes an idempotent re-assert, not the first assignment; discarding `store` from the exclusions also puts the `store` FK back into `full_clean`'s `clean_fields()` pass — one extra existence query against `stores.Store` per admin POST, safe (Store is the tenant root, not a StoreOwnedModel; FK.validate uses the target's plain manager).

**`core/admin.py` — wiring (D2), extend the already-auto-mixed `StoreScopedFormFieldsMixin`:**

```python
# on StoreScopedFormFieldsMixin (reached by every ModelAdmin on both sites
# via _StoreScopedRegistrationMixin.register, and by every inline via
# StoreOwnedInlineMixin):

def get_form(self, request, obj=None, change=False, **kwargs):
    form_class = super().get_form(request, obj, change=change, **kwargs)
    return self._wrap_store_unique_validation(request, form_class)

def get_formset(self, request, obj=None, **kwargs):
    # Only exists on InlineModelAdmin subclasses; ModelAdmin never calls it.
    formset_class = super().get_formset(request, obj, **kwargs)
    formset_class.form = self._wrap_store_unique_validation(
        request, formset_class.form, inline=True
    )
    return formset_class

def _wrap_store_unique_validation(self, request, form_class, inline=False):
    if not issubclass(self.model, StoreOwnedModel):
        return form_class
    if "store" in form_class.base_fields:
        return form_class  # stock Django validates natively (DiscountCodeSuperAdmin pattern)
    if issubclass(form_class, StoreUniqueValidationFormMixin):
        return form_class
    store = None
    if not inline and self.admin_site is not super_admin_site:
        store = getattr(request, "store", None)  # HostResolutionMiddleware
    return type(
        form_class.__name__,
        (StoreUniqueValidationFormMixin, form_class),
        {"_pradize_validation_store": store, "__module__": form_class.__module__},
    )
```

**`core/formsets.py` — inline parent-store injection (D2), on `StoreSafePKFormSetMixin`:**

```python
def _construct_form(self, i, **kwargs):
    form = super()._construct_form(i, **kwargs)
    parent = getattr(self, "instance", None)  # BaseInlineFormSet only
    if (
        parent is not None
        and issubclass(self.model, StoreOwnedModel)
        and form.instance.store_id is None
    ):
        store = parent if isinstance(parent, Store) else getattr(parent, "store", None)
        if store is not None and store.pk is not None:
            form.instance.store = store
    return form
```

Ordering makes this sound on both sites: options.py:1847-1852 runs `save_form()` (→ the parent instance, with store injected by the form mixin on the store site or picked in the form on the super site) **before** `_create_formsets(request, new_object, …)` constructs the inline formsets, so the parent's store exists by inline-validation time even on an add page. Extra blank inline forms are unaffected (`empty_permitted` + unchanged → `full_clean` returns early). The existing `save_formset` store-assignment conventions stay as idempotent re-asserts.

**D5 — CsvListWidget 3-part fix (`core/widgets.py`, `catalog/models.py`, `stores/models.py`):**

The TICKET-052 finding understates it: this is a *corruption* bug, not just display. Today `JSONField.prepare_value()` (django/forms/fields.py:1382) unconditionally `json.dumps()`s, so an empty `tags` renders the textarea as the literal text `[]`; on any save, `CsvListWidget.value_from_datadict()` CSV-splits that text into `['[]']` — a no-op save of an untouched product writes `tags=['[]']`.

1. `CsvListFormField.prepare_value(value)`: `if isinstance(value, list): return value` (else `super()`). This finally lets `CsvListWidget.format_value()`'s list branch render `red, blue`, and `''` for `[]`. Round-trip on bound redisplay already works via the existing `bound_data` override (returns the parsed list → `prepare_value` passes it through).
2. `CsvListFormField.to_python(value)`: `if value in self.empty_values: return []` (else `super()`). Without this, an empty submission cleans to `None` and `construct_instance` writes `None` into a `null=False` JSONField — "This field cannot be null" (or an IntegrityError if validation is skipped).
3. `blank=True` on `catalog.Product.tags` and `stores.Organization.coverage_areas_json` + the two generated migrations (validation-only; no SQL schema change). Without it, part 1 unmasks emptiness and the fields' own model default (`[]`) fails "This field is required".

Data cleanup: a one-off check for already-corrupted rows (`tags`/`coverage_areas_json` containing the literal string `'[]'`) — data migration if any exist in real data, otherwise a note in the ticket close-out.

## Affected surface (snapshot — the drift test is the authority, not this list)

Definition: every ModelAdmin/inline on `store_admin_site` whose model is a `StoreOwnedModel` carrying at least one store-inclusive `unique_together`/`UniqueConstraint`, with `store` not in the form (i.e. all of them, per the convention). Current snapshot: pages.StaticPage `(store, slug)` and StaticPageTranslation `(store, page, lang_code)`; catalog.Product `(store, slug)`, ProductTranslation `(store, product, lang_code)`, Collection `(store, slug)`, StockNotification `(store, product, email)`, and the inline family ProductVariant `(Upper('sku'), store)`, ProductOption `(store, product, name)`, ProductOptionValue `(store, option, value)`, VariantOptionAssignment `(store, variant, option)`, ProductImageTranslation/OptionTranslation/OptionValueTranslation `(store, …, lang_code)`; discounts.DiscountCode `(store, code)` (store-site admin); pixels.Pixel `(store, provider)`; emails.EmailTemplate `(store, template_id)`; engagement.SocialProofSettings `(store)`, LeadCaptureCampaignTranslation `(store, campaign, lang_code)`, LeadSignup `(store, campaign, email)`; campaigns.CampaignStepTranslation `(store, step, lang_code)`; chat.StoreChatSettings `(store)`; consent.ConsentSettings `(store)`; currency.StoreCurrencySetting `(store, currency)`; feeds.FeedConfig `(store, provider)` and FeedLanguageCountry `(store, provider, store_language, country_code)`; cart.Cart `(store, session_key)`; orders.Order `(store, order_number)`; customers.Customer `(store, email)`; the stores.* per-store constraint rows (StoreDomain/StoreLanguage/ShippingCountry/StoreEmployee). Read-only/inbox admins are structurally covered but unreachable in practice.

## Risks

- **Behavior change on change forms:** editing a row into a same-store collision now produces a form error instead of a 500 — strictly better, but any test asserting the 500 must be updated (none known; TICKET-052's regressions assert the friendly path).
- **`request.store` absent on the store site** (super admin browsing /admin/ with no resolved store): injection is skipped, behavior degrades to exactly today's (and `get_queryset` already returns `.none()` there). No new failure mode.
- **Extra queries:** one Store-FK existence check per POST (clean_fields, see D1 note) + one `exists()` per store-inclusive check — the check stock Django would always have run. Negligible; admin-only.
- **Race window remains (D4):** two concurrent identical submissions can both validate then one 500s. Accepted; stock-Django posture; revisit only on observed ops data.
- **Class-attribute store on a per-request form class:** safe only because `get_form`/`get_formset` build fresh classes per call (modelform_factory/inlineformset_factory). Pinned by a test asserting two wraps with different stores don't share state.
- **D5 `blank=True`:** makes the two fields optional in *every* ModelForm, not just admin — that is the fields' actual semantics (their default is `[]`), so no spec conflict.

## Rollback strategy

Pure code except the two D5 `blank=True` migrations (no SQL — reverse-able no-ops). Reverting the single commit restores today's behavior exactly: 500s on same-store duplicates, JSON-text rendering in CsvList textareas. No data rollback needed (the D5 cleanup migration, if any, only removes literal `'[]'` corruption tokens — keep it as its own commit so it survives a mixin revert).

## Tests required

**Drift test — decision: structural-semantic sweep, not full-HTTP-per-admin.** Full HTTP per admin would need a valid POST fixture for every required field of ~30 models, a permanent maintenance tax with little marginal signal over the sweep below plus canaries. Chosen shape (`core/tests/test_admin_unique_validation_sweep.py`, extending the `_iter_admins_and_inlines()` pattern of `test_raw_id_widget_sweep.py` / `test_admin_form_field_scoping_sweep.py`):

1. Enumerate, for every admin/inline on both sites, models where `Model()._get_unique_checks(include_meta_constraints=True)` (plus expression UniqueConstraints, which that call misses) yields a store-inclusive check; assert a floor count (≥ 20), not an exact list.
2. Store site, per admin: build the form via `admin.get_form(request_with_store)`; assert `"store" in base_fields` OR the class is a `StoreUniqueValidationFormMixin` subclass with `_pradize_validation_store == request.store`; then the **semantic** core — instantiate bound (`form_class(data={})`), run `is_valid()`, assert `form.instance.store_id == store.pk` and `"store" not in form._get_validation_exclusions()`. No per-model fixtures needed: exclusion computation is independent of the form being otherwise invalid.
3. Inlines: same via `get_formset(request).form`, plus one assertion that `StoreSafePKFormSetMixin._construct_form` stamps the parent store.
4. Super site (D3): every add-capable ModelAdmin over a constrained StoreOwnedModel has `"store"` in `base_fields`; violations must be listed explicitly in the test to be grandfathered (loud, §XV-1).

**HTTP canaries (2):** `StaticPageAdmin` duplicate `(store, slug)` ADD POST over the real store-site endpoint → 200 + field/non-field error, not 500 — this is the exact reproduction TICKET-052 documented as failing; one inline canary (duplicate `ProductOption` name in a single ProductAdmin POST) proving both the per-instance check and the formset cross-form dedup path.

**Guard-rails:** same-code-different-store still validates cleanly through the same admin endpoint (cross-store semantics unchanged — mirrors `CrossStoreConstraintSemanticsUnchangedTest`); `DiscountCodeSuperAdmin` HTTP regression from TICKET-052 stays green (proves the `"store" in base_fields` short-circuit); change-form collision (edit slug into an existing one) → form error; no-leak test for `_pradize_validation_store` across two wraps.

**D5 regressions** (extend `core/tests/test_csv_list_form_field_regression.py`): GET renders `red, blue` for `["red","blue"]` and `''` for `[]`; no-op save of empty tags round-trips `[]` (the corruption repro, must fail on current code); empty submission validates and saves `[]`; bound redisplay after an unrelated error still renders `red, blue`.

**After-Bug proof cycle** (mandatory, BUG_TESTS.csv row): revert the `_get_validation_exclusions` discard alone → sweep item 2 and the StaticPage canary fail; revert the injection alone → same canary fails via the None-skip (proves BOTH primitives are load-bearing); revert `prepare_value` → D5 display/corruption tests fail; restore → full suite green.
