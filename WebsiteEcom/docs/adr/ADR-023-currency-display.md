# ADR-023: Currency display converter (storefront) and admin screen

**Status:** ACCEPTED (human, 2026-07-11; proposed 2026-07-10). Implements `TICKET-031` (storefront
display conversion) and `TICKET-037` (admin/super-admin currency screen).
Resolves the shared PENDING blocker on both tickets ("rate source/frequency
and rounding rules") and `11_uncertainties_to_validate.md` Part B "Currency:
display-only vs transactional conversion… rate source/frequency, rounding".
**Addendum DECIDED (human, 2026-07-11):** `show_code`/`code_placement`
("Code visibility", admin-011) restored to §2's `CurrencyDefinition` table —
see the §2 addendum below — after being dropped between the screenshot spec
and this ADR's original draft.

**Extends (frozen — not re-opened here):**
- ADR-001 §3b/§5 — `default_currency` stays a plain field on `Store`; this
  ADR does not touch it. §5 already classifies "currencies" as
  **platform-global** — this ADR is the concrete design for that entity,
  replacing the placeholder `Currency [G]` row sketched in
  `05_database_schema.md` §12 (single-table sketch, now split into four
  entities — see §7 below).
- ADR-015 (`docs/adr/ADR-015-checkout.md`) — **ML-004, already shipped**:
  checkout and thank-you amounts display in the store's transactional
  currency only, with a one-line notice
  (`storefront/partials/checkout_sidebar.html` `currency-notice`,
  `views_checkout.py` `order_currency = store.default_currency`). This ADR
  does not touch that code path; it only supplies the display-currency
  conversion for the pages *before* checkout. See §5 for the exact boundary
  and a correction to `08_acceptance_tests.md` AC-160's wording, which
  predates ML-004 and contradicts it on one point.
- ADR-015 addendum (sf_lang unconditional-write bug) — the binding invariant
  that no fixed/funnel-continuation route may have a session key written on
  it unconditionally by middleware. §4 below designs the currency-selection
  write path to never create this problem in the first place, rather than
  adding a second exemption list.
- `stores/middleware.py` `StoreScopedManager` / `StoreOwnedModel` (ADR-001 §4)
  — the per-store enablement table in §2 uses this base class like every
  other store-scoped model.

**Binding constraints (design-pattern-ideas):**
- §XV-1 (invisible failures are the enemy) — a bad or stale rate must be
  visible somewhere. §1 below places that visibility at the correct layer:
  the admin/super-admin dashboard, not the storefront (AC-160 explicitly
  forbids surfacing staleness to the shopper — see §5). This is a deliberate,
  documented scoping of §XV-1, not a violation of it.
- §XV-4 (single resolution function) — exactly one function answers "what
  rate and formatting apply for currency X on store Y right now"; the
  template tag, the catalog-feed converter (`FEED-C1`), and any future report
  all call it. No second copy of a conversion formula.
- §XIII (atomic decrement pattern) — rates are **read-only at request time**;
  nothing about display conversion decrements a counter, so the check-then-act
  race this pattern warns about does not arise here. It matters instead for
  the *refresh* job (§1): a bad fetched value must never silently overwrite
  every store's displayed prices platform-wide (blast radius — see §1
  "sanity bound").
- §IV/§VII (explicit job state, stable IDs) — the rate-refresh Celery task
  writes an explicit per-currency result (`ok` / `error` with message), never
  a single boolean for the whole run; a partial provider outage (12 of 31
  currencies returned) must not look identical to full success.

---

## 1. D1 — Rate source

**Decision:** **manual entry is always authoritative and always available**;
**optional automatic refresh** from the European Central Bank's free,
no-auth, no-signup daily reference-rate feed, fetched with the Python
standard library only (`urllib.request` + `xml.etree.ElementTree` —
**zero new dependencies**). Auto-refresh is **off by default**; a super-admin
opts in per currency-converter-wide toggle. When on, a Celery beat task
(matching the existing pattern in `webecom/settings/base.py`
`CELERY_BEAT_SCHEDULE`) refreshes rates daily.

**Options considered:**

| Option | Tradeoff |
|---|---|
| **Manual only** | Zero moving parts, zero external dependency, zero reliability risk — but every rate goes stale the moment a currency moves and nobody remembers to update it (silent staleness, exactly the AC-160 "2 days old" scenario, except with no refresh path at all). |
| Paid/keyed API (exchangerate-api.com, Open Exchange Rates, …) | Broader currency coverage and better SLAs, but requires an API key (a new `requirements.txt` dependency for a decent client, or hand-rolled `urllib` + `json` — either way, a paid tier and a secret to store) — needs approval per the global "no new dependency without approval" rule and introduces a billing dependency for a **P2/P3, complexity-S** ticket. Rejected for v1 as disproportionate. |
| **ECB daily reference rates (chosen), auto-refresh optional** | Free, no key, no signup, no rate limit, stable XML endpoint (`https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml`) publishing ~31 major currencies daily against EUR. Covers most of the ticket's 34-currency target (see §6, PENDING: exact extra currencies). Parseable with two stdlib modules — no `requests`, no new line in `requirements.txt`. Does not cover minor/regional currencies a merchant might add later; those stay manual-only, which is an explicit, visible state (`CurrencyRate.source = 'manual'`), not a silent gap. |
| Scrape a rate-comparison website | No stable contract, ToS risk, brittle to markup changes — rejected outright. |

**Why manual-first, not auto-first:** the ticket is **display-only** and
**complexity S** — over-building a refresh pipeline as the load-bearing path
would violate "do not over-engineer simple features." Manual entry is the
floor that always works (a super-admin who does not trust or want an external
call for money-adjacent data can run the whole feature manually forever).
Auto-refresh is additive, optional, and — because it costs zero dependencies
— nearly free to include, but it is architected as a **plugin**, not the
only path, per the registry pattern mandated for pluggable connectors.

**Design contract — `RateSource` registry**, same shape as the payments
connector interface already in `payments/processor.py`
(`authorize`/`capture`/`refund`/`health_check`) and the `SlotProvider`
interface in `storefront/slots.py` (`key`, `name`, `required_settings`,
`is_enabled`, `validate_settings`, `render`):

```
class RateSource(ABC):
    key: str                     # 'manual' | 'ecb' | (future) 'exchangerate_api'
    name: str                    # human-readable, admin UI
    required_settings: list[str] # e.g. [] for ecb (no key needed)

    def is_enabled(self) -> bool: ...
    def validate_settings(self) -> list[str]: ...   # launch-checklist-style messages
    def run(self) -> RateFetchResult: ...            # fetch + return per-currency results
    def health_check(self) -> HealthResult: ...       # last run status, for the admin dashboard
```

- `ManualRateSource` (`key='manual'`): `run()` is a no-op (there is nothing to
  fetch — the admin edits `CurrencyRate.rate_to_reference` directly through
  the admin form). Always `is_enabled()`. This is the entry that makes "no
  rate source configured" impossible — it is the permanent fallback, not an
  optional registry member.
- `EcbRateSource` (`key='ecb'`): `run()` fetches the XML, parses
  `(currency_code, rate_vs_eur)` pairs, and returns a
  `RateFetchResult(currency_code -> Ok(rate) | Error(message))` map — **per
  currency**, not one pass/fail for the whole call (§IV/§VII: explicit
  sub-state, no all-or-nothing masking of partial failure).
- **Sanity bound before writing (blast-radius guard — this table is
  platform-global, so a bad fetched value would mis-price every store at
  once):** a fetched rate is applied only if
  `0.01 * previous_rate <= new_rate <= 100 * previous_rate` (guards against a
  decimal-shift or garbled-feed value) **and** `new_rate > 0`. A rejected
  value keeps the last-known-good rate, sets
  `CurrencyRate.last_refresh_error` (human-readable), and is counted in the
  task's per-run error list — visible on the admin dashboard, per §XV-1,
  never silently substituted.
- The Celery task (`payments`/`campaigns`-style task module, e.g.
  `currency/tasks.py::refresh_currency_rates`) is idempotent and safe to
  re-run: it re-fetches and re-applies the same sanity-checked write path
  regardless of how many times or how recently it last ran (§XV-6). No
  distributed lock is needed — writes are per-row, not a single counter.

**Staleness — visible to the admin, deliberately invisible to the shopper.**
`CurrencyRate.updated_at` plus a platform setting
`CURRENCY_RATE_STALE_HOURS` (default 48h, only meaningful for `source='api'`
rows — a manual rate never auto-expires; the admin owns its freshness) drive
an `is_stale` computed property. This powers:
- The T037 admin/super-admin dashboard: a "last updated Xh ago" badge per
  currency, red when stale, plus the last refresh run's per-currency error
  list. This is the §XV-1 visibility.
- **Nothing on the storefront.** AC-160's own text is explicit: *"the
  displayed price uses the cached rate without surfacing staleness to the
  shopper."* That is a deliberate, spec-approved simplicity choice for a
  **display-only, informational** number — the shopper is never charged the
  displayed amount (§5). §XV-1's "invisible failures are the enemy" is
  satisfied because the failure (a stale rate) *is* visible — in the
  dashboard the person who can act on it uses — not because every surface
  must show every warning.

**Risks:** ECB's feed occasionally omits a currency for a day (holiday
publishing gaps) — the sanity-bound-rejection path already handles "no new
value" by keeping the last-known rate, so a missed publish day degrades to
"one day staler," never to "wrong price." Network failures in the Celery task
are caught, logged at ERROR, and leave rates untouched (never partially
applied).

**Rollback:** disable the "Auto-refresh" toggle in the admin screen — the
system instantly reverts to manual-only with the last-fetched rates frozen in
place (no data loss, no migration).

**Tests required:** ECB XML parsing (well-formed, malformed, empty, missing
currency), sanity-bound rejection (rate 100x/0.001x previous — must reject
and log; rate 0 or negative — must reject), idempotent re-run (two
consecutive task runs with unchanged upstream data produce no duplicate
writes), staleness badge computation at the boundary (`updated_at` exactly at
`CURRENCY_RATE_STALE_HOURS`), manual-source rows never flagged stale,
Celery task network-failure path leaves all rows untouched.

---

## 2. D2 — Data model

**Decision:** four entities, replacing the single placeholder `Currency [G]`
row in `05_database_schema.md` §12:

| Entity | Scope | Key fields | Notes |
|---|---|---|---|
| `CurrencyDefinition` | `[G]` platform-global | `code` (ISO 4217, unique), `name`, `symbol`, `symbol_placement` (prefix/suffix), `show_code` (bool, default `False`), `code_placement` (prefix/suffix, default `suffix`), `decimal_places` (2 for most; 0 for JPY/KRW-style currencies — **required** for correct rounding, see §6), `is_active` | Super-admin CRUD at `/superadmin/` (ADR-001 §3b pattern — same ownership tier as `Theme`, `ShippingZone`, `ShippingCarrier`). Deactivating removes it from every store's enable list without deleting history. |

**Addendum (human decision 2026-07-11) — `show_code`/`code_placement` restored.**
The screenshot-derived spec (`part_B_admin_006-012.md`, admin-011) specifies a
"Code visibility" control (Yes/No + Prefix/Suffix segmented buttons) alongside
"Symbol visibility" (`symbol_placement`, already in the model above) — e.g.
Canadian Dollar renders `"45.99 $ CAD"` (symbol per `symbol_placement`, ISO
code appended per `code_placement`). This was dropped between the screenshot
spec and this ADR's original D2 table; the human reviewing the spec→ADR chain
on 2026-07-11 confirmed it must ship as originally specced, not silently
narrowed. `CurrencyDefinition` gains `show_code` (bool, default `False` — every
existing/seeded currency keeps today's rendering unless a super-admin opts
in) and `code_placement` (reuses the `SymbolPlacement` prefix/suffix choices,
default `suffix`, matching the AUD/BRL screenshot rows). Formatting composes
independently of `symbol_placement` in the single formatter
(`currency/templatetags/currency_tags.py::_format_amount`) — no second copy of
the price-formatting logic, consistent with §XV-4. Additive migration, no
backfill required.
| `CurrencyRate` | `[G]` platform-global | `currency` (FK, unique), `rate_to_reference` (`Decimal(18,8)`), `source` (`manual` \| `api:ecb`), `updated_at`, `previous_rate`, `last_refresh_error` | One row per `CurrencyDefinition`, 1:1. All rates are anchored to a single fixed **reference currency = EUR** (matches the ECB feed's native anchor — zero cross-rate math for the auto path). The EUR row itself is the anchor: `rate_to_reference = 1.00000000`, `source='manual'`, never touched by the refresh task. |
| `StoreCurrencySetting` | `[S]` store-scoped (`StoreOwnedModel`) | `currency` (FK to `CurrencyDefinition`), `is_enabled` | `unique_together (store, currency)`. The store's own transactional currency (`Store.default_currency`) needs **no row here** — it is implicitly always available and is never itself "converted" (rate 1:1, no rounding pass) when a shopper selects it as display currency. |
| `CurrencyConverterSettings` | `[G]` platform-global, singleton | `auto_refresh_enabled` (bool, default `False`), `rounding_mode` (`none` \| `psychological_99`, default `psychological_99`), `stale_hours` (default 48) | Enforced singleton: `pk` pinned to `1` in `save()`, `get_solo()` classmethod — same well-known pattern used for other rarely-changed platform-wide toggles; no existing singleton model precedent in this codebase, so this ADR establishes the first one deliberately rather than smuggling the fields onto `CurrencyDefinition` (which is per-row, not global) or into Django `settings.py` (which the ticket requires to be admin-editable — "rate-source configuration and refresh cadence" per T037). |

**Cross-rate computation (the single resolver, §XV-4):**

```
def display_amount(base_amount: Decimal, store_currency: str, display_currency: str) -> Decimal:
    if display_currency == store_currency:
        return base_amount  # identity — no rate lookup, no rounding pass
    rate_store = CurrencyRate for store_currency  (1.0 if store_currency == 'EUR')
    rate_display = CurrencyRate for display_currency
    raw = base_amount * (rate_display / rate_store)
    return apply_rounding(raw, CurrencyDefinition for display_currency)
```

This single function is the only place multiplication happens. The T031
template tag (§4), the T033 per-country catalog feed converter (already
DECIDED, `FEED-C1`: *"CA-targeting feed shows CAD prices converted from store
default USD using the currency converter's exchange rate… feed regenerated
when rates update"*), and any future report all call this function — never
re-derive a cross rate inline. `FEED-C1`'s "regenerated when rates update"
is satisfied by the existing event-driven feed regeneration (`TICKET-033`,
same debounce pattern as `TICKET-026`) listening for `CurrencyRate.save()`.

**Why platform-global rates, not per-store:** `TICKET-031`'s own description
says *"Currency data is platform-global"*, and ADR-001 §5 already classifies
"currencies" as platform-global alongside carriers and email template
masters. A EUR/JPY rate is a fact about the world, not about a store; letting
each store carry its own copy would mean 50 stores drift to 50 slightly
different EUR/JPY rates with no reason, and the ECB refresh task would need
to fan out writes to every store instead of one row. Per-store control is
expressed only through *enablement* (`StoreCurrencySetting`) and through the
store's own `default_currency`, which was already store-scoped before this
ADR and is untouched by it.

**Why not fold rate + definition into one row:** `CurrencyDefinition`
(symbol, placement, decimals) changes essentially never; `CurrencyRate`
changes daily under auto-refresh. Splitting them means the refresh task
touches one narrow table with one arguably-hot column
(`rate_to_reference`, `updated_at`), and an admin editing a currency's symbol
never risks a concurrent write conflicting with the refresh job's row lock.

**Risks:** a currency deactivated at `CurrencyDefinition.is_active=False`
while a store still has `StoreCurrencySetting(is_enabled=True)` for it must
resolve to "not offered" at the store, not error — `is_enabled()` in §4
checks both flags. Migration risk: none — this is new tables, no
backfill of existing data (the only prior currency-shaped field,
`Store.default_currency`, is untouched).

**Tests required:** singleton enforcement (`get_solo()` never creates a
second row under concurrent calls — `select_for_update` inside `get_solo`),
`display_amount()` identity path when `display_currency == store_currency`
(no rate lookup, so a currency with zero `CurrencyRate` rows still lets a
shopper see prices in their own store's currency), cross-rate math against
EUR anchor with known values, deactivated-currency-still-enabled-at-store
resolves to unavailable.

---

## 3. D3 — Currency catalog and per-store enablement surfaces (admin split)

**Decision:** correcting `specs/ecommerce_engine/06_settings_requirements.md`
§8's placement. That section places the whole "Currency Converter" screen at
`/admin/` — but per **ADR-001 §3b/§5, already accepted**, currencies are
explicitly platform-global, owned by the platform super-admin, the same tier
as Themes and Shipping Zones. This ADR follows that precedent instead of §8's
placement, the same way `ADR-017` corrected a spec numbering mismatch inline
rather than re-litigating it:

- **`/superadmin/`** — the master `CurrencyDefinition` list (add/edit/deactivate
  a currency: code, symbol, placement, decimal places), the `CurrencyRate`
  table (manual rate entry, or read-only display + "last refreshed"/error
  info when `source='api:ecb'`), and `CurrencyConverterSettings` (the
  auto-refresh toggle and rounding mode — this **is** T037's "rate-source
  configuration and refresh cadence").
- **`/admin/`** (per store) — a simple enable/disable checklist against the
  platform's `CurrencyDefinition` list, writing `StoreCurrencySetting` rows.
  No symbol/rate editing here — exactly mirroring how a store *activates* a
  platform-owned `Theme` without editing the theme's shared definition.

This resolves T037's own area tag ("17. Currency conversion / **27.
Super-admin settings**") in favor of the super-admin placement, and treats
`06_settings_requirements.md` §8's "/admin/" wording as superseded for the
master-data half, correct only for the store-side enablement half.

**Also superseding `06_settings_requirements.md` §8's "one global enable
toggle" framing:** there is no single platform- or store-wide on/off switch
for the currency converter feature. Enablement is entirely per-currency —
each `StoreCurrencySetting.is_enabled` row is its own toggle, and a store with
**zero** enabled rows is equivalent to "off" (the picker does not render, see
the launch-readiness note below). This one-line correction replaces §8's
"one global enable toggle" framing rather than adding a new, redundant
platform-wide boolean next to the per-currency checklist that already exists.

**Launch-readiness checklist impact:** none required by this feature.
`06_settings_requirements.md` already has `L13` ("Default store currency
set", Tier 1 blocking — unaffected, still just `Store.default_currency`) and
`R7` ("At least one currency enabled", Tier 2 — **non-blocking**). This ADR
implements `R7` as: `StoreCurrencySetting.objects.filter(store=store,
is_enabled=True).exists()`. A store may launch and sell with zero display
currencies enabled — the picker (§4) simply does not render, and every price
shows in the store's own currency. This matches the ticket's P2/P3 priority
and "display-only" framing: it is explicitly not launch-blocking.

**Tests required:** store-admin enable/disable round trip does not affect
`CurrencyDefinition`/`CurrencyRate` rows (permission boundary — store admin
has no write access to those tables, verified by a permission test, not just
UI hiding); super-admin deactivating a `CurrencyDefinition` cascades to "not
offered" at every store without deleting `StoreCurrencySetting` rows (history
preserved for audit/re-enable).

---

## 4. D4 — Shopper currency selection and session persistence

**Decision:** header picker only (component `C-07` in
`15_storefront_theme_system.md`, already specced as "display-only, AC-161").
**No geo-IP auto-detection** — there is no geo-IP infrastructure anywhere in
this codebase (confirmed by the ticket brief and by inspection), and adding
one is out of scope for a complexity-S ticket. Choice persists in
`request.session['display_currency']`, written **only** by one explicit,
narrow endpoint — never by middleware on every page view.

**Why an explicit endpoint and not middleware — avoiding the sf_lang bug
class by construction.** ADR-015's addendum documents a real, shipped bug:
`LocaleMiddleware` wrote `session['sf_lang']` unconditionally on *every*
resolved request, including fixed funnel-continuation routes
(`/checkout/`, `/cart/`, …), silently clobbering the shopper's real value
with the domain's root language before checkout could read it. The fix
required a maintained exemption list (`SF_LANG_WRITE_EXEMPT_PREFIXES`) plus a
drift test walking the URLconf, because the correct exemption set is not
mechanically derivable.

Currency selection has no reason to inherit that risk: there is no
per-request "resolve the ambient currency from the URL" concept analogous to
locale resolution (currency is never encoded in the path, unlike language).
So instead of writing `display_currency` on every request and building a
second exemption list, **only one view writes it**:

- `POST /currency/set/` (view name `storefront:set-display-currency`),
  registered before the `<path:slug>` catch-all like every other fixed
  storefront route (matching the D3 endpoint precedent in `ADR-017`).
  Body: `{currency: code, next: <redirect path>}`. Validates `code` against
  `StoreCurrencySetting.objects.filter(store=request.store,
  currency__code=code, is_enabled=True)`; on success writes
  `request.session['display_currency'] = code` and 302s to `next` (falls
  back to `/` if `next` is missing/unsafe — standard open-redirect guard,
  same as Django's own `LoginView`). On failure (unknown/disabled code):
  redirects back to `next` without changing the session — never errors, per
  the never-break-the-page-for-money-adjacent-features principle running
  through this ADR.
- **No other code path writes this key.** There is nothing to add to
  `SF_LANG_WRITE_EXEMPT_PREFIXES`-style exemption lists, and no drift test is
  needed for this key, because there is no broad automatic writer to exempt
  anything from in the first place. This is called out explicitly so a
  future Developer does not "helpfully" add a middleware write for
  consistency with `sf_lang` — that would reintroduce exactly the bug class
  ADR-015's addendum fixed for language.
- **Read path** — `resolve_display_currency(request, store) -> code`:
  `session.get('display_currency')` if it is still enabled for *this* store
  (a shopper could have picked a currency at Store A that Store B does not
  offer — Django sessions are cookie/domain-scoped per site already, so
  cross-store leakage is not the concern; the concern is the *same* session
  cookie persisting across a redirect chain that changes store, or a store
  admin disabling a currency after the shopper picked it); else
  `store.default_currency` (identity, no conversion — never geo-guesses, never
  errors). Single resolver function, called from the context processor (§5)
  and nowhere else.
- The picker's own template only ever needs `store`'s enabled
  `StoreCurrencySetting` list plus the current resolved code to render its
  option list and highlight the active one.

**Risks:** a shopper who never explicitly picks a currency simply sees
`store.default_currency` forever — no surprise conversions, no reliance on
a browser `Accept-Language`/IP heuristic that is often wrong for VPN users
and shared devices. This is the documented v1 trade-off (§6, ASSUMPTION).

**Tests required:** POST with a disabled/unknown code leaves session
unchanged and redirects safely; POST with a valid enabled code persists and
redirects; open-redirect guard on `next`; resolver falls back to
`default_currency` when the session's code is no longer enabled at the
current store; **regression test proving `display_currency` is never present
in `session` after a bare request to `/checkout/`, `/cart/`, `/orders/…`,
etc. with no prior POST to `/currency/set/`** — i.e., prove there is no
hidden middleware write, mirroring the spirit of ADR-015's
`test_sf_lang_write_exemption_drift.py` but by proving absence-of-a-writer
rather than maintaining an exemption list.

---

## 5. D5 — Scope of conversion (what shows converted, what never does)

**Decision, confirming and making explicit what ADR-015/ML-004 already
shipped:**

| Surface | Currency shown | Why |
|---|---|---|
| Product page, collection/search cards, home | **Display currency** (resolved §4), via `display_amount()` (§2) | Browsing surfaces — pure information, no money changes hands here. |
| Cart page (`cart.html`) / floating cart | **Display currency** | Still pre-checkout browsing/intent, per `AC-160`'s own example ("cart totals… display in EUR"). `Cart.currency` (the DB field, defaulted `'USD'`) is **not** the display currency and is untouched by this feature — it is the transactional currency the checkout math already runs in. *(Pre-existing gap noted, not fixed by this ADR: `Cart.currency` is never actually set to `store.default_currency` anywhere in `cart/checkout.py`/`cart/service.py` — it just carries the model default. `views_checkout.py`'s own `order_currency` context variable already bypasses it and reads `store.default_currency` directly, which is the pattern this ADR also follows. Fixing `Cart.currency`'s assignment is out of scope here — flagged for a future ticket.)* |
| **Checkout (address + payment) and thank-you page** | **Store's transactional currency only** (`store.default_currency`) — **no conversion, ever** | **Already shipped, ADR-015 ML-004**: "*Display-currency conversions available while browsing… do not apply at checkout*", with the existing one-line notice ("You will be charged in USD"). This ADR does not touch `checkout_sidebar.html`, `checkout.html`, or `thank_you.html` currency rendering. |
| Order record, processor charge | **Store's transactional currency**, always | `AC-161`, unaffected by this feature — no schema change to `orders/models.py` or `payments/`. |
| Order confirmation / all transactional emails | **Store's transactional currency**, always | Same reasoning as the order record — the email reads the same amount that was charged, never a display-converted figure. |
| Per-country catalog feeds (Facebook/Google, `T033`) | **Display currency of the target country**, per `FEED-C1` (already DECIDED) | Uses `display_amount()` (§2), the same single resolver — no separate feed-side conversion formula. |

**Correcting `08_acceptance_tests.md` `AC-160`'s wording against the shipped
ADR-015 decision:** `AC-160`'s "Then" clause says *"…all collection page
prices, cart totals, **and checkout subtotals** display in EUR"*. The
"checkout subtotals" clause **contradicts** `ML-004` (checkout shows the
charge currency only, plus a notice) and predates it — `16_checkout.md`'s
`ML-004` and its implementation in `views_checkout.py`/
`checkout_sidebar.html` are the later, ACCEPTED, already-shipped decision and
win. `AC-160` should be read as applying to product/collection/cart pages
only; the checkout behavior is governed by `ML-004`/`AC-161` instead. (Same
correction style as `ADR-017`'s CO-003/ML-003 spec-ID note — flagged inline
rather than re-opening either spec file.)

**Tests required:** a regression test asserting the checkout and thank-you
pages render zero currency-conversion output regardless of
`session['display_currency']` — i.e., prove the boundary in §5 holds even
when a shopper arrives at checkout having picked EUR on the product pages;
existing checkout-currency-notice tests are unaffected and must stay green.

---

## 6. D6 — Rounding and the ".99" disclaimer

**Decision:** a single deterministic function, `apply_rounding(raw: Decimal,
currency_def: CurrencyDefinition) -> Decimal`, used everywhere `display_amount()`
is used (§2, §XV-4). All arithmetic is `Decimal`, never `float` (matches every
existing money field in `orders/models.py`/`catalog/models.py`, all
`DecimalField`).

- `rounding_mode = 'none'` (`CurrencyConverterSettings`, §2): plain rounding
  to `currency_def.decimal_places` (`ROUND_HALF_UP`).
- `rounding_mode = 'psychological_99'` (default): let `u = 10 ^
  -decimal_places` (the smallest unit — `0.01` for 2-decimal currencies,
  `1` for 0-decimal currencies like JPY/KRW). Then:
  ```
  if raw <= u:
      return raw.quantize(u, ROUND_HALF_UP)   # guard below — see edge case
  whole = raw.quantize(Decimal('1'), rounding=ROUND_CEILING)  # next whole unit at/above raw
  return whole - u
  ```
  This reproduces both halves of `AC-162` with one formula: a non-integer
  raw value (e.g. `45.9908`) ceilings to `46`, minus `0.01` → `45.99`
  (`AC-162`'s main case); a raw value that is already an exact whole number
  (e.g. `46.00`) also ceilings to itself (`46`), minus `0.01` → `45.99`
  (`AC-162`'s explicit "would round down to €45.99, not display X.00" case).
  This is standard retail psychological pricing: the result is the highest
  "X.99"-ending price **at or below the next whole unit**, which is usually
  *above* the raw converted value, not below it — intentional (§AC-162 confirms
  this is the desired behavior, not a bug).
- **Zero/near-zero guard:** the `raw <= u` branch above exists because the
  ceiling-minus-unit formula would otherwise produce a **negative** displayed
  price for a free line item (e.g. a buy-X-get-X free item, or an order-bump
  discounted to `$0.00` — both explicitly "100% free, no cap" per the DECIDED
  up-sell rules). A `$0.00` line must convert to `€0.00`, never `-€0.01`.
  This is exactly the class of "invisible failure" §XV-1 warns about — it
  would only surface as a shopper support ticket about a negative price on a
  free gift.
- **0-decimal currencies (JPY, KRW, …):** the same formula applies at
  whole-unit granularity (`u = 1`), producing "one unit below the next whole
  number" (e.g. `¥5001` → `¥5000`). This is a **generic** generalization of
  the 2-decimal ".99" convention, not the locale-specific psychological
  convention some JPY/KRW retailers actually use (e.g. prices ending in
  `…98` or `…80`). Getting the per-locale convention exactly right is a
  merchandising nuance, not an architecture blocker — flagged as
  **ASSUMPTION** for v1 (generic rule), **PENDING** product sign-off before
  any 0-decimal currency is actually enabled for a real store (§8).
- **Disclaimer:** every display-converted price partial must show an
  "approximate" note (e.g. "≈ converted from USD, informational only") —
  the ticket's `AC-160`/`AC-161` dangerous-data notes plus ML-004's "was
  informational only" language both point the same direction. This is a
  copy/template requirement handed to the Developer + Designer as part of
  the `T031` ticket, reusing the same partial pattern as the existing
  mandatory "All taxes included" note (`product_card.html`, `cart.html`,
  `product.html`) — same visual weight, same always-adjacent-to-price rule.

**Rounding mode is platform-global** (`CurrencyConverterSettings.rounding_mode`),
not per-store or per-currency, for the same "one behavior, one place" reason
as §XV-4 — a merchant who wants unrounded conversion for all display
currencies flips one setting; there is no per-currency override in v1
(no evidence any spec surface asked for one; adding it later is a single new
field, not a redesign).

**Tests required:** the four `AC-162` cases verbatim (non-integer → `.99`
below ceiling; exact-whole → `.99` below itself; consistency across
cart/collection/checkout-adjacent surfaces — modulo §5's checkout exclusion,
so really: collection card vs cart line vs product page, same currency, same
source amount, byte-identical output); zero-amount guard; 0-decimal currency
formula; `rounding_mode='none'` plain-rounding path; `Decimal`-only arithmetic
(a test asserting no `float` appears in the call chain is out of scope —
covered instead by code review + the existing project-wide `DecimalField`
convention).

---

## 7. Database schema update

Supersedes the placeholder single-row sketch in `05_database_schema.md` §12
(`Currency [G] | code, symbol, symbol_position, show_code, is_enabled,
exchange_rate, rate_updated_at, rounding_rule (.99) | — | Display-only vs
transactional conversion PENDING… Rate source/frequency PENDING`). Replace
with the four-entity split from §2 above; both PENDING notes on that row are
now resolved by this ADR (display-only — confirmed §5; rate source — §1).

---

## 8. Open items carried to `11_uncertainties_to_validate.md`

1. **PENDING (MEDIUM, product) — exact 34-currency list.** ECB's daily feed
   natively covers ~31 currencies (EUR + 30). The remaining ~3 to reach the
   ticket's "34 currencies" figure will be manual-rate-only under this
   design. Which markets fill that gap (candidates: AED, SAR, COP, or others)
   is a merchandising decision, not an architecture one — Architect
   recommends confirming the target-market list alongside the shipping
   zones/countries already configured, but does not block `T031`/`T037`
   implementation (manual entries work identically to auto ones from
   day one).
2. **PENDING (LOW, product) — 0-decimal currency psychological rounding
   convention.** §6's generic "-1 whole unit" rule is a reasonable default
   but not necessarily the retail-correct convention per locale. Only
   matters once a 0-decimal currency (JPY, KRW, …) is actually enabled for a
   real store — no launch blocker.
3. **ASSUMPTION (kept as default, not escalated) — picker-only currency
   selection, no geo-IP auto-detection.** Confirmed in the ticket brief
   itself ("no geo-IP infra exists — likely picker only for v1"); revisit
   only if a future ticket adds geo-IP infrastructure for an unrelated
   reason.
4. **ASSUMPTION (kept as default) — auto-refresh off by default.** A
   super-admin must explicitly opt into the ECB auto-refresh; every store
   ships with manual-only rates until someone flips the toggle. This trades
   a small amount of staleness risk for zero surprise external calls on a
   fresh install.
