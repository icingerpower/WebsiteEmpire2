# ADR-017: Checkout address i18n — country metadata, adaptive form, subdivision & postal validation

**Status:** ACCEPTED (human, 2026-07-10; proposed 2026-07-06). Implements spec
`specs/ecommerce_engine/16_checkout.md` SA-002 (relabel-on-country-change part),
SA-004 (state/province subdivision select), SA-005 (per-country postal validation),
ML-003 (country-adaptive field labels).

> Spec-ID note: the task brief referenced "CO-003" for adaptive labels; in
> `16_checkout.md` CO-003 is the phone field. The adaptive-labels requirement is
> **ML-003**, invoked by SA-002 ("Changing the country … relabels fields (ML-003)").
> This ADR uses the spec's numbering.

**Extends (frozen — not re-opened here):**
- ADR-015 — checkout flow, `CheckoutState`, single-page checkout, the address
  snapshot shape `{name, line1, line2, city, state, postal_code, country}`
  (SA-001; also frozen in `orders/models.py` Order snapshot docs), NFR-1
  (core flow works without JavaScript; JS is progressive enhancement only).
- ADR-016 — cart lifecycle (untouched).
- SA-002 country-choice sourcing from active shipping zones
  (`views_checkout._build_country_choices`) — reused as-is.

**Binding constraints (design-pattern-ideas):**
- §XV-4 (single resolution function) — exactly ONE module answers "what are the
  address rules for country X"; the form, the template, the AJAX endpoint and
  the tests all read it. No second copy of a postal regex anywhere.
- §XV-3 (state explicit, never inferred) — a country with no subdivision is an
  explicit `subdivision_mode='none'` entry, not a missing key silently treated
  as free-text.
- §XV-5 (validation at persistence boundaries) — per-country validation runs
  server-side in the form that writes `CheckoutState.shipping_address`,
  regardless of what the client-side JS did or didn't do.
- §II (NFD normalization) — not directly applicable (no slugs), but subdivision
  label matching must be case- and accent-insensitive using the same
  NFD-strip helper as the slug utility (single source of truth).

---

## 1. Context

`AddressForm` (`storefront/forms_checkout.py`) is a flat form: `state` is an
optional free-text field with a hardcoded "State / Province" label, and
`postal_code` accepts any non-empty string ≤ 20 chars. The checkout template
(`storefront/templates/storefront/pages/checkout.html`) hardcodes the labels
with `{% trans %}`. The docstring in `forms_checkout.py` already flags this as
"a v1 approximation: in full SA-003/SA-004/SA-005, country selection also
drives adaptive label rendering and postal-code pattern validation."

Consequences today:
- A US shopper can submit `postal_code="abc"` and `state=""` — the order ships
  with an unusable address; carriers reject it downstream where it is expensive.
- A UK shopper sees "State / Province" and "Postal code" instead of "County"
  and "Postcode" — trust-killing at the highest-stakes step of the funnel.
- Countries with mandatory subdivisions (US/CA/AU at minimum per SA-004) get a
  free-text field, producing `"NY"`, `"ny"`, `"New York"`, `"new york"`
  variants in order snapshots — breaking any later per-region analytics,
  tax work, or carrier API integration.

This ADR decides four things: the country-metadata data source (D1), the form
architecture (D2), the country-change AJAX contract (D3), and the storage
schema (D4). It is scoped so a Developer can implement it in a single ticket.

---

## 2. D1 — Data source for country metadata

**Decision:** Option A — a hand-curated static Python module,
`storefront/address_meta.py`, covering ~30 launch markets, with an explicit
documented fallback for every other country.

**Options considered:**

| Option | Tradeoff |
|---|---|
| **A. Static module (chosen)** | Zero dependencies; offline-deterministic tests; one file to review; must be hand-maintained (but the dataset is near-frozen: ISO subdivisions for major markets change ~never) |
| B. `pycountry` | Full ISO-3166-2 (~5000 subdivisions) but provides **no postal regexes and no field labels** — half the metadata would still need a hand-curated layer, so the dependency buys only subdivision lists we can vendor in one file; adds a package to `requirements.txt` (needs approval per global DoD) and a data-version drift axis |
| C. `django-localflavor` | Per-country *form fields*, not metadata — incompatible with a single dynamic form (D2); would force 30+ imports and per-country form wiring; maintenance status of individual country modules is uneven |

**Why A:** the deciding fact is that **no library provides the complete tuple
we need** (subdivisions + postal regex + localized labels + subdivision mode).
Any option ends with a hand-curated layer; A makes that layer the *only* layer
(§XV-4 single source of truth). Pradize storefronts only offer countries with
active shipping zones (SA-002), so the effective country set is small and
merchant-controlled; ~30 curated entries cover all realistic launch markets.
Tests never touch the network or a package's data files.

**Shape of the module (design contract, not code):**

- `CountryAddressMeta` — frozen dataclass:
  - `country: str` — ISO 3166-1 alpha-2, the dict key.
  - `subdivision_mode: str` — one of `'select' | 'text' | 'none'` (explicit
    tri-state, §XV-3). `'select'` ⇒ `subdivisions` is non-empty and the value
    is **required**; `'text'` ⇒ optional free text; `'none'` ⇒ field hidden,
    stored as `''`.
  - `subdivisions: tuple[(code, label), ...]` — bare ISO 3166-2 suffix codes
    (`'NY'`, not `'US-NY'` — country is stored separately, and bare codes are
    what Stripe/PayPal/carrier APIs expect in the `state` slot).
  - `subdivision_label: lazy str` — `gettext_lazy`: "State" (US), "Province"
    (CA), "County" (GB, optional), …
  - `postal_pattern: str | None` — anchored regex applied **after
    normalization** (see below). `None` ⇒ non-empty check only (SA-005
    "otherwise non-empty").
  - `postal_label: lazy str` — "ZIP code" (US), "Postcode" (GB/AU),
    "Postal code" (CA), "Code postal" (FR), … Labels are `gettext_lazy` so the
    checkout language (U16-3) still translates them; the per-country label
    picks the *term*, gettext picks the *language*.
  - `postal_example: str` — placeholder/hint ("10001", "SW1A 1AA", "100-0001").
- `DEFAULT_META` — the explicit fallback entry (`subdivision_mode='text'`,
  `postal_pattern=None`, generic labels). `get_country_meta(code)` **never
  raises and never returns None** — unknown country ⇒ `DEFAULT_META`. This is
  the rollback and forward-compat story in one: a country absent from the dict
  behaves exactly like today's v1 form.
- `get_country_meta(country_code: str) -> CountryAddressMeta` — the single
  resolver (§XV-4). Uppercases/strips its input.
- `normalize_postal_code(country, raw) -> str` — strip, uppercase, collapse
  internal whitespace; per-country canonicalization where unambiguous (GB/CA:
  single internal space; JP: insert hyphen after 3 digits when 7 bare digits
  given). Returns the value to validate AND store.
- `normalize_subdivision(meta, raw) -> str | None` — for `'select'` mode:
  accept a bare code (case-insensitive) **or** a label (case- and
  accent-insensitive via the shared NFD-strip helper), return the canonical
  code; return `None` when no match (caller raises the form error). This keeps
  the no-JS path honest: a shopper typing "new york" into the fallback text
  input still produces `'NY'`.

**Launch dataset (subdivision `'select'` rows):** US, CA, AU, MX, BR, IT, ES,
JP, AR, IN, MY, TH — plus `'text'`/`'none'` entries with postal patterns for
GB, FR, DE, NL, BE, AT, CH, IE, PT, SE, DK, NO, FI, PL, CZ, NZ, SG, KR, ZA.
(Exact list finalized in the ticket; the architecture is indifferent to the
membership — adding a country is adding one dict entry plus one test row.)

**Risks:** curated data can contain a wrong regex (e.g. new UK postcode
districts). Mitigation: postal patterns are deliberately permissive within the
documented national format (SA-005 is *format-only* — no deliverability
check), and `DEFAULT_META` degradation means a wrong-but-removed entry can
never block checkout for that country.

---

## 3. D2 — Form architecture

**Decision:** hybrid of options A and C, dictated by NFR-1: a **single
`AddressForm`** whose **validation** is driven entirely by the *submitted*
country inside `clean()` (server-authoritative, option C's backend half), and
whose **rendering** (widget choice + labels) is configured in `__init__` from
the best-known country (option A's dynamic-fields half). Live country changes
are progressive enhancement via JS + the D3 endpoint. **No form re-render
round-trip on country change.**

**Options considered:**

| Option | Tradeoff |
|---|---|
| A. Single form, `__init__(country=…)` swaps fields; AJAX returns new form HTML | Server-rendered fragment would clobber the shopper's already-typed line1/city/postal on every country change (or require fragile client-side field-value merging); validation tied to the country the form was *built* with, not the one *submitted* — wrong when the shopper changes country and submits in one no-JS POST |
| B. Per-country subclasses (`AddressFormUS`, …) | 30 classes for ~4 varying attributes; adding a country = new class + view wiring + template awareness; the view must map country→class *before* validation, same chicken-and-egg as A; violates "adding a country is one dict entry" |
| **C+A hybrid (chosen)** | One form; validation reads `cleaned_data['country']` so it is always correct for what was actually submitted (works with JS disabled, works when country changed mid-form); rendering hints from `__init__` are cosmetic only |

**Why:** NFR-1 is the forcing function. With JavaScript disabled the sequence
is: shopper picks country and fills everything, one POST. The server never had
a chance to rebuild the form for that country — so **per-country validation
must live in `clean()`, keyed off the submitted country**, or it is theater.
Once validation is submission-driven, per-country form *construction* buys
nothing for correctness; it only matters for rendering the right widget and
labels, which `__init__` can do from a hint.

**Design contract:**

- `AddressForm.__init__(…, country_choices=None)` — unchanged signature plus
  internal render-country resolution: bound data `country` value → `initial`
  country → store default country → none. From the resolved render-country's
  meta:
  - `state` field: `subdivision_mode='select'` ⇒ `ChoiceField`-style widget
    with the meta's `(code, label)` choices **plus an empty option**;
    `'text'` ⇒ `TextInput`; `'none'` ⇒ `HiddenInput` (field kept in the form —
    never deleted — so bound-data handling and `to_address_dict()` stay
    uniform). `required` is left `False` at field level; requiredness is
    enforced in `clean()` (see below) so the widget choice can never
    accidentally relax or tighten validation.
  - `state.label` = meta `subdivision_label`; `postal_code.label` = meta
    `postal_label`; `postal_code` widget gets `placeholder=postal_example`
    and `pattern=postal_pattern` attrs (free client-side hint, zero trust).
- `AddressForm.clean()` — the authoritative path (§XV-5). Reads
  `cleaned_data['country']`, fetches meta **once**, then:
  1. `postal_code` ← `normalize_postal_code(country, raw)`; if
     `postal_pattern` set and no full match ⇒ field error on `postal_code`
     using the meta's label and example ("Enter a valid ZIP code (e.g. 10001)").
  2. `state`: mode `'select'` ⇒ required; `normalize_subdivision` must return
     a code, which **replaces** the raw value in `cleaned_data` (canonical
     code is what gets stored). Mode `'text'` ⇒ optional, stripped. Mode
     `'none'` ⇒ forced to `''` regardless of input (a stale value from a
     previous country selection must not leak into the snapshot).
  - Validation is in `clean()`, not `clean_postal_code()`/`clean_state()`,
    because both depend on `country` and Django's per-field clean order must
    not become a hidden dependency (the `country` field is currently declared
    *after* `postal_code`).
- Template change: `checkout.html` replaces the hardcoded
  `{% trans "State / Province" %}` / `{% trans "Postal code" %}` labels with
  `{{ address_form.state.label }}` / `{{ address_form.postal_code.label }}`,
  and wraps the state row in a `data-` container the JS can show/hide. The
  form becomes the single label authority (§XV-4).
- The view (`checkout_view`, `checkout_address_post`, and the two other
  re-render sites that construct `AddressForm`) passes nothing new — the form
  self-resolves its render-country from bound/initial data. Store default
  country hint: `getattr(store, 'registration_country', '')` is **not** used
  (it is the merchant's legal country, not the market); the render-country
  simply stays unresolved when unknown, yielding `DEFAULT_META` rendering
  until the shopper picks a country.

**Adding a new country** = one entry in `address_meta.py` + one row in the
parametrized test table. No form, view, or template change. This is the
maintainability bar the rejected options fail.

**Risks:** the widget shown after a *server* render can mismatch the country
if JS is on and the shopper changed country without the JS running (race on
slow load). Harmless: validation is submission-driven; worst case the shopper
types free text that `normalize_subdivision` maps to a code, or gets a clear
field error naming the expected format.

---

## 4. D3 — Country-change endpoint

**Decision:** a **JSON metadata endpoint**, not an HTML-fragment re-render.

`GET /checkout/address/country-meta/?country=US`

- **GET, not POST** — pure idempotent metadata lookup; no CSRF dance; cacheable.
  Registered in `storefront/urls.py` **before the `<path:slug>` catch-all**,
  like every other checkout URL. Name: `checkout-address-country-meta`.
- Response `200 application/json`, labels rendered in the active checkout
  language (the lazy strings resolve under the request's translation, U16-3):

```json
{
  "country": "US",
  "subdivision_mode": "select",
  "subdivision_label": "State",
  "subdivisions": [["AL", "Alabama"], ["AK", "Alaska"], ...],
  "postal_label": "ZIP code",
  "postal_pattern": "^\\d{5}(-\\d{4})?$",
  "postal_example": "10001"
}
```

- Missing/unknown `country` param ⇒ `200` with the `DEFAULT_META`
  serialization (`subdivision_mode: "text"`, `postal_pattern: null`) — the JS
  never needs an error branch, mirroring `get_country_meta`'s never-fails
  contract. The endpoint does **not** check the country against the store's
  shipping zones: shippability is the country *choices*' job (SA-002, enforced
  by `ChoiceField` validation on POST); metadata is harmless public data.
- `Cache-Control: public, max-age=86400, Vary: Accept-Language` — static data,
  language-sensitive labels.
- `checkout.js` enhancement: on `change` of the country select — fetch, then
  (1) swap the two label texts, (2) set the postal input's `placeholder` and
  `pattern`, (3) for `'select'` replace the state input with a `<select>`
  (name/id/autocomplete `address-level1` preserved, previous value re-selected
  when still valid), for `'text'` restore a text input, for `'none'` hide the
  row and clear the value. All other typed fields untouched.

**Why JSON over an HTML fragment:** (1) the shopper's typed line1/city/postal
survive by construction — a fragment swap would destroy or complicate them;
(2) one rendering path — the fragment would be a second template rendering the
same fields, guaranteed to drift from `checkout.html`; (3) the payload is ~40
lines vs a form render, and cacheable per country+language. The existing
`checkout.js` already uses `fetch`; this stays in its idiom.

---

## 5. D4 — Storage schema

**Decision:** **keep the existing snapshot shape and the key name `state`.**
No schema change, no key rename, no migration of existing blobs.

```json
{
  "name": "Jane Doe",
  "line1": "…", "line2": "…",
  "city": "…",
  "state": "NY",
  "postal_code": "10001",
  "country": "US"
}
```

- **`state`, not `subdivision`/`region`:** the shape
  `{name, line1, line2, city, state, postal_code, country}` is frozen in three
  places — spec SA-001, ADR-015, and the `orders/models.py` Order snapshot
  contract — and is already persisted inside **immutable Order snapshots**.
  Renaming the key would require rewriting historical order snapshots (which
  are audit artifacts and must not be rewritten) or a dual-key read layer
  forever. The semantic upgrade is in the *value*, not the key: for
  `subdivision_mode='select'` countries it now always holds the canonical bare
  ISO 3166-2 suffix code (`"NY"`); for `'text'` countries, stripped free text;
  for `'none'`, `""`.
- **Not nullable — empty string `""` for no-subdivision countries.** Matches
  every existing writer (`to_address_dict` defaults to `''`), keeps consumers
  free of `None` checks, and is itself explicit state: `''` under a
  `mode='none'` country means "correctly absent" (§XV-3 — the meta module, not
  the blob, is what says whether a subdivision was expected).
- `postal_code` is stored **normalized** (output of `normalize_postal_code`).
- `country` remains ISO 3166-1 alpha-2 (unchanged, SA-001).
- `CheckoutState.shipping_address`, `CheckoutState.billing_address` (copy
  semantics per SA-007/U16-5), `Order.shipping_address`,
  `Order.billing_address` all keep this one shape — `begin_checkout` and the
  thank-you/retry paths need **zero changes**.

**Migration path for existing blobs: none required, by construction.** Old
blobs (`state` = arbitrary free text, `postal_code` = un-normalized) remain
schema-valid because the schema is unchanged and validation happens **only at
the form boundary on write**, never on read. In-flight `CheckoutState` rows:
the shopper either proceeds (address already past validation under the old
rules — acceptable; orders in flight are not retro-blocked) or re-opens the
address section, where re-submission re-validates under the new rules.
Historical Orders are immutable snapshots and are deliberately left as-is.

---

## 6. Implementation sketch (one Developer ticket)

New:
1. `storefront/address_meta.py` — `CountryAddressMeta`, `COUNTRY_ADDRESS_META`
   (~30 entries), `DEFAULT_META`, `get_country_meta`, `normalize_postal_code`,
   `normalize_subdivision`. All labels `gettext_lazy`. Module has no Django
   model imports — pure data + functions (trivially testable).
2. `views_checkout.country_meta_view` — `@require_GET`, serializes
   `get_country_meta`, sets cache headers. URL in `storefront/urls.py` before
   the catch-all: `checkout/address/country-meta/`.

Modified:
3. `storefront/forms_checkout.AddressForm` — render-country resolution in
   `__init__` (widget + labels from meta), authoritative `clean()` per §3.
   `to_address_dict()` unchanged (cleaned_data already canonical).
4. `storefront/templates/storefront/pages/checkout.html` — labels from the
   form (`{{ address_form.state.label }}`, `{{ address_form.postal_code.label }}`),
   state row wrapped in `data-address-state-row`, postal input gains
   placeholder/pattern from the form widget attrs (no template logic).
5. `storefront/static/storefront/js/checkout.js` — country-change listener per
   §4 (progressive enhancement; zero behavior when JS off).

Nothing else moves: `checkout_address_post`, `begin_checkout`, `CheckoutState`,
`Order`, shipping resolution, and all four `AddressForm` construction sites
keep their signatures.

---

## 7. Rollback strategy

- Data: none needed — no schema/key change; snapshots written under this ADR
  are valid under the old reader and vice versa.
- Behavior: deleting (or emptying) `COUNTRY_ADDRESS_META` reverts every country
  to `DEFAULT_META`, which reproduces today's exact behavior (free-text state,
  non-empty postal, generic labels) with the new code paths still in place.
  Per-country rollback = delete that one entry. The JSON endpoint degrades to
  serving `DEFAULT_META` — the JS then performs no visible change.

---

## 8. Tests required

Unit — `address_meta` (no DB):
- Parametrized over every entry: postal pattern accepts the documented example
  and rejects a canonical bad value; `'select'` entries have non-empty
  subdivisions; every `subdivision_mode` is one of the three literals.
- `get_country_meta`: unknown code / empty / lowercase input → `DEFAULT_META`
  or correct entry; never raises.
- `normalize_postal_code`: GB `"sw1a1aa"` → `"SW1A 1AA"`; JP `"1000001"` →
  `"100-0001"`; US `"10001-1234"` passthrough.
- `normalize_subdivision`: code case-insensitive (`"ny"` → `"NY"`); label
  match (`"new york"` → `"NY"`); accent-insensitive label (e.g. MX
  `"Yucatán"`/`"yucatan"`); no match → `None`.

Unit — `AddressForm`:
- US: missing state → error; `state="ny"` → cleaned `"NY"`; `postal="abc"` →
  error naming "ZIP"; `postal="10001-1234"` valid.
- GB: postcode regex enforced; state optional.
- Unknown country: today's behavior (any non-empty postal, optional state).
- `mode='none'` country: submitted stale state value is blanked in
  `cleaned_data`.
- Widget/labels: form built with US initial renders select + "State"/"ZIP
  code"; with no country renders text + generic labels.
- No-JS honesty test: form **built without a country hint** but **bound** with
  `country="US"` still enforces US rules (the D2 forcing case).

Integration — views:
- `country-meta` endpoint: US payload contract (all six keys), unknown country
  → DEFAULT payload, cache headers present, resolves before the slug catch-all.
- `checkout_address_post`: invalid US ZIP re-renders with the bound form
  showing the US widget/labels; valid post stores canonical
  `state="NY"` + normalized postal in `CheckoutState.shipping_address`.
- Label language: endpoint called under a French checkout language returns
  translated labels (`Vary: Accept-Language` honored).

Regression:
- Existing checkout flow tests (contact→address→shipping→pay) pass unchanged
  for a shipping-zone country not in the metadata dict (proves DEFAULT_META
  degradation).

---

## 9. Out of scope

- SA-003 Google Maps address autocomplete (separate integration + settings
  registry work).
- External address-validation/deliverability services (Loqate, SmartyStreets…)
  — SA-005 is explicitly format-only.
- Full ISO 3166-2 coverage beyond the curated launch markets (add-on-demand,
  one dict entry each).
- SA-006 `CustomerAddress` save-for-later (Phase-5 accounts).
- Separate billing-address collection (SA-007/U16-5 — unchanged copy
  semantics; when the U16-5 override form ships it reuses this same
  `AddressForm` and inherits everything here for free).
- Geo-IP default country preselection (SA-002 second half — separate concern:
  needs a geo-IP source decision).
