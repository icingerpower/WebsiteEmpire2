# ADR-019: Shipping service description field (checkout SM-002 / U16-7 / Settings-C5)

No prior ADR owns the `ShippingZone`/`ShippingRate` schema as its primary subject — ADR-001
§3b only fixes platform-vs-store ownership; ADR-015 (checkout) only *consumes* shipping via
`resolve_shipping_rates`. This is a new, short ADR rather than an addendum to either.

## ADR-019: Shipping service description field

**Status: DECIDED (human, 2026-07-10) — zone-level, per spec SM-002/Settings-C5 literal text.**

**Decision:** Add `service_description` (TextField, `blank=True, default=""` — no `null`)
to **`ShippingZone`** (`shipping/models.py`), as spec 16 SM-002 and Settings-C5 state.
Single-language for v1 (matches existing precedent for short admin-configured
customer-facing strings); tracked as multilingual debt, not built as a one-off translation
table.

**Context:**
Spec 16_checkout.md SM-002 requires checkout to show a configured "shipping service
description" (e.g. "Ships from EU warehouse — no customs") in the shipping-method radio
list when set. Settings-C5 (`06_settings_requirements.md:1177`) and
`11_uncertainties_to_validate.md` U16-7 place the field on shipping zones.

`ShippingZone` is **platform-global, super-admin-owned** (`shipping/models.py` docstring,
ADR-001 §3b): every store shipping to a zone shares the same row, and zones are edited at
`/superadmin/` only. The Architect initially recommended moving the field to the store-owned
`ShippingRate` to avoid sharing text across stores; the human reviewer decided to keep the
zone-level placement per the spec text.

**Options considered:**
1. `ShippingZone.service_description` — matches the spec text verbatim; one description per
   geographic zone, shared by every store using that zone, super-admin-edited.
2. `ShippingRate.service_description` — store-owned placement; was the Architect's original
   recommendation (avoids sharing text across stores, allows per-rate variation within a
   zone). **Considered and rejected: the human reviewer chose literal spec compliance
   (option 1) on 2026-07-10.** Human decision — overrides the recommendation.
3. Zone-level field with a store-level override table (mirrors
   `Theme` → `StoreThemeCustomization`) — over-engineered for one text field; rejected per
   the "do not over-engineer simple features" rule.

**Chosen option: 1 — `ShippingZone.service_description` (human decision).**

**Why:** Direct compliance with SM-002/Settings-C5 as written. The zone is where the spec's
examples naturally live — they describe the *logistics of the zone* ("Ships from EU
warehouse — no customs", "Delivered by our refrigerated fleet" for a refrigerated-network
zone), not store marketing.

**Accepted consequences (explicit):**
- **Store admins can NOT edit this text.** `ShippingZone` is registered on
  `super_admin_site` only (`shipping/admin.py`); the field is super-admin-owned like the
  rest of the zone.
- **The text is shared by every store shipping to that zone.** Two unrelated stores using
  the "EU" zone show the same description at checkout.
- **Mitigation:** super-admins must keep the copy operational and store-neutral — logistics
  facts about the zone ("Ships from EU warehouse — no customs", delivery-network
  properties), never store-branded marketing ("Our artisan team hand-packs every order").
  This constraint goes in the field's `help_text` so it is visible at the point of edit.

**Multilingual:** Single-language for v1. Precedent: `StoreThemeCustomization.announcement_text`
(ADR-012, `stores/models.py`) is the closest analog — a short, admin-configured,
customer-facing string — and it has **no** per-language mechanism today (unlike
`hero_images`, which explicitly resolves per-language variants via the T003 media pipeline).
The heavyweight translation pattern used for full content objects (`ProductTranslation`,
`StaticPageTranslation` — per-language row, AI job triggers, permalink lifecycle, ADR-014/
ADR-018) is scoped for pages and products with SEO surface area; building an equivalent
table for one short field is disproportionate.
**Known multilingual debt (add to this list on future occurrences, do not spin up a new
translation table per field):**
- `StoreThemeCustomization.announcement_text` (ADR-012) — single-language.
- `ShippingZone.service_description` (this ADR) — single-language.

Resolves U16-7 and Settings-C5 as: **DECIDED zone-level (human, 2026-07-10); translation
DEFERRED to debt list, not blocking.**

**Admin exposure:** `shipping/admin.py` — `ShippingZoneAdmin` (super-admin site only). The
admin has no explicit `fields`/`fieldsets` today, so the field appears automatically on the
zone form; Developer should list it explicitly next to `name`/`country_codes` for
readability. No store-admin exposure (accepted consequence above).

**Migration:** Trivial — one `TextField(blank=True, default="")` on `shipping.ShippingZone`.
No `null=True` (Django convention: avoid NULL on text fields), no backfill, no index.

**Risks:**
- Cross-store text sharing (accepted consequence above) — if a future merchant genuinely
  needs store-specific copy, the escape hatch is option 3 (store-level override table), a
  backwards-compatible later addition.
- Long descriptions could visually crowd the compact rate row; no length limit is
  specified — Developer should cap the widget at a reasonable `maxlength` in the admin form
  (soft cap, not a DB constraint) and confirm with Designer during the UI pass.
- Repetition: when several rates in the radio list belong to the same zone, the description
  renders once per rate. SM-002 specifies display per rate, so render per rate as specced;
  Designer may dedupe visually later without a schema change.

**Rollback strategy:** Revert the migration (`manage.py migrate shipping <previous>`) and
the template conditional; nothing else references `service_description`, so removal is
isolated and non-destructive.

**Tests required:**
1. Model/migration: `ShippingZone.objects.create(...)` without `service_description`
   succeeds (field optional); saving with a value round-trips.
2. Checkout template: `data-testid="shipping-service-description"` renders the zone's text
   for a rate whose zone has `service_description` set, and the element is absent (not
   empty) when the zone's field is blank — assert via the checkout shipping-step test
   alongside existing SM-002/SM-003 tests.

## Implementation sketch (one Developer ticket)

- **Model:** `shipping/models.py` — add to `ShippingZone`:
  `service_description = models.TextField(blank=True, default="", help_text="Optional customer-facing note shown at checkout for rates in this zone, e.g. 'Ships from EU warehouse — no customs'. Shared by ALL stores shipping to this zone — keep it operational and store-neutral, never store-branded.")`
- **Migration:** `shipping/migrations/00XX_shippingzone_service_description.py` —
  autogenerated `AddField`, no backfill.
- **Admin:** `shipping/admin.py` — `ShippingZoneAdmin` (super-admin site): add
  `"service_description"` to an explicit `fields` list next to `name`/`country_codes`
  (or leave implicit; explicit preferred). No store-admin change.
- **Template:** `storefront/templates/storefront/pages/checkout.html`, inside the
  `{% for rate in shipping_rates %}` loop (~line 186), after the ETA span and before the
  price span — sourced via the rate's zone FK. No extra query:
  `resolve_shipping_rates()` already does `.select_related("zone", "carrier")`
  (`shipping/service.py` ~line 51), so `rate.zone.service_description` is free.
  ```html
  {% if rate.zone.service_description %}
  <p class="shipping-rate__service-description" data-testid="shipping-service-description">
    {{ rate.zone.service_description }}
  </p>
  {% endif %}
  ```
- **Tests:** one model test (`shipping/tests/`) for the optional field round-trip; one
  template/view test (`storefront/tests/test_checkout_*.py`, alongside existing
  shipping-step assertions) for the `data-testid` present (zone description set) / absent
  (zone description blank) cases.
