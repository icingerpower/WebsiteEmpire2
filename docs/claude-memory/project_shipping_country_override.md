---
name: project-shipping-country-override
description: "ShippingCountry platform-defaults + per-store override feature — resolver, consumers rewired, store toggle UX, broad dropship default list"
metadata:
  type: project
---

Shipping-country platform-defaults + per-store override, shipped 2026-09-06
(commits 562d2b6, 6f51d51, 431e0c2, 88e8b0e, 4ac93b5, 714f0e4). Triggered by
Cédric seeing an empty store-admin ShippingCountry page and wanting a
platform-level default that a store can optionally override.

**Model/design:**
- `stores/models.py`: `PlatformDefaultShippingCountry(lang_code, country_code)`
  (unique_together) + `Store.override_shipping_countries` (BooleanField, default
  False = INHERIT). Defaults are keyed by **lang_code**, not store: a
  non-overriding store inherits, for EACH of its StoreLanguages, the countries
  whose lang_code matches. Migrations 0022 (schema) + 0023 (data: set
  override=True for stores that already had own ShippingCountry rows —
  behaviour-preserving). ShippingCountry stays `models.Model` scoped via
  store_language FK (NOT StoreOwnedModel).
- **Single resolver `stores/shipping_targets.py`** — consumers MUST go through it,
  never read the rows directly: `effective_shipping_country_codes(store_language)`,
  `store_language_has_target_market(sl)`, `store_targets_country(store, cc)`,
  `enabled_store_languages_with_target_market(store)`. Batches platform defaults
  by lang_code; request-cached where used in SEO.

**Consumers rewired to the resolver:** SEO (permalinks/hreflang, sitemaps,
permalinks_tags) — Stage 2a; product-feed matrix (feeds/tasks _reconcile_matrix),
currency/autodetect _language_targets, discounts/validators _store_targets_fr,
engagement/service is_de_targeting_store — Stage 2b.

**Store-admin UX (Stage 1b):** ShippingCountry changelist banner + POST view
`set_shipping_mode_view` (URL name `stores_shippingcountry_set_mode`): switch
inherit↔override. No pk in URL → mutates only request.store → cross-store flip
impossible by construction. POST-only, CSRF, `check_module_access("domains","full")`
+ `has_change_permission`, open-redirect-safe. Revert never deletes own rows.
Banner (CSP-safe, ADR-045) shows inherited countries per language in inherit
mode; empty-markets warning in override-with-zero-rows.

**Feeds note (jobs-checker):** no signal on Store.override / PlatformDefault
changes, but NOT a gap — `_reconcile_matrix` runs unconditionally on every beat
(≤30 min eventual consistency, targets born dirty). Same latency as any
ShippingCountry edit.

**Default country list decision (Cédric, 2026-09-06):** default = ship to every
affordable/reliable China-dropship country; exclude war/sanctioned zones (UA, RU,
BY, IR, IQ, SY, YE, AF, MM, VE, KP, CU, SD/SS, LY, SO, ML) and tiny/expensive
islands (Pacific micro-states, most small Caribbean, remote specks, Maldives).
Seeded ~104 countries × in-use lang codes into the **:8099 scratch DB only**
(see [[project_local_8099_scratch_env]]) — NOT yet a shipped default for all envs.
**PENDING Cédric's call:** (1) SEO nuance — per-language defaults mean a non-English
edition also claims hreflang+feeds for all 104 countries; offered a ship-broad /
hreflang-language-appropriate split if wanted. (2) Durability — needs a seed
mechanism (mgmt command or auto-seed on new language) to be the real default
everywhere; architect+dev change, not done. Relates to [[project_admin_polish_sweep]].
