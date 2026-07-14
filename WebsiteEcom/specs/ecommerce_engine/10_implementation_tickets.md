# 10_implementation_tickets

> Implementation backlog — owned by Architect Agent, executed by Developer Agent.
> Inputs: `02_feature_matrix.md` (priorities), `09_architecture_decisions.md` (ADR-001…007, ADR-004 revised for high volume), DECIDED items in `11_uncertainties_to_validate.md`, `08_acceptance_tests.md` (AC references).

**Status: FIRST FULL BACKLOG — 2026-07-02. REVISED 2026-07-03: safety amendments applied (T007, T010-CART, T013, T028).**

## Phase definitions

- **Phase 1 — Technical Foundation (~4 months):** Django project structure, data models, admin, auth, discount engine, analytics pipeline, payment routing, AI job infrastructure, URL resolution, payment connectors. Nothing sellable at end of Phase 1; that is intentional.
- **Phase 2 — First Sellable MVP (~3 months):** Storefront, themes, transactional email, shipping, SEO/sitemap, abandoned checkout, pixel firing, product feeds, currency display, Amazon FBA.
- **Phase 3 — Polish (~4 months):** AI chat, bookkeeping API, additional payment processors, advanced analytics.

(Confirmed 2026-07-03: Phase 1 = foundation; Phase 2 = first sellable release.)

## Conventions

- **Phase 1** = core foundation (tickets 001–020, implement in numbered order — no forward dependencies).
- **Phase 2** = key features (tickets 021–037, after Phase 1 stable).
- **Phase 3** = polish & integrations (tickets 038–047).
- **Complexity:** S (≤2 days) / M (3–5 days) / L (1–2 weeks) / XL (>2 weeks).
- **Hard sequencing rules honored:** AI Job System (ADR-003) and analytics pipeline (ADR-004) are early cross-cutting infrastructure; payment routing (ADR-006, T017–T020) lands before the upsell funnel (ADR-007, T027–T028) because the upsell capture-window depends on delayed-capture connectors and routing decisions.
- **PENDING blockers** cite `11_uncertainties_to_validate.md` items and part files. A ticket with blockers can usually *start* (models, non-blocked paths) but cannot *finish* until the blocker is decided — exceptions noted per ticket.

### Summary

| Phase | Tickets | S | M | L | XL |
|---|---|---|---|---|---|
| 1 — Core foundation | 20 | 2 | 11 | 7 | 0 |
| 2 — Key features | 17 | 4 | 9 | 3 | 1 |
| 3 — Polish & integrations | 12 | 1 | 7 | 4 | 0 |
| **Total** | **49** | **7** | **27** | **14** | **1** |

Rough effort: 7×2 + 27×4 + 14×7.5 + 1×12 ≈ **239 dev-days** (~11 months single developer; ~4 months with 3 parallel developers respecting the dependency DAG). TICKET-048/049 (consent management, ADR-025, added 2026-07-11) account for the +2 tickets / +9 dev-days versus the prior 230.

---

# Phase 1 — Core foundation

### TICKET-001: Django project setup + multi-tenant enforcement skeleton
**Phase:** 1
**Priority:** P1
**Area:** 28. Multi-tenant architecture / 1. Auth & Permissions
**Depends on:** none
**ADRs referenced:** ADR-001
**Spec files:** 02, 06
**Estimated complexity:** M
**Description:** Bootstrap the Django project: two `AdminSite` instances (`/admin/` per-store, `/superadmin/` cross-org), custom `User` model (email login, `is_store_admin`, `is_super_admin`), abstract `StoreOwnedModel` base (`store` FK, `on_delete=PROTECT`), and `StoreScopedManager` whose `get_queryset()` **raises** unless called via `.for_store(store)` or `.cross_store_unsafe()` (ADR-001 §4 — the unscoped query must be the loud failure). Include host-resolution middleware stub (`request.store`), admin store-switcher middleware, the shared slug function module (NFD normalize per ADR-005 §1, wrapping Django `slugify`), CI, and the model-introspection unit test that fails when a store-scoped model lacks the base class.
**Acceptance criteria references:** AC-003, AC-004, AC-200, AC-202
**PENDING blockers:** none.

### TICKET-002: Multi-tenant models — Store, Organization, User, StoreEmployee
**Phase:** 1
**Priority:** P1
**Area:** 28. Multi-tenant architecture / 1. Auth & Permissions
**Depends on:** TICKET-001
**ADRs referenced:** ADR-001, ADR-006
**Spec files:** 02, 04, 06
**Estimated complexity:** M
**Description:** Implement `Store` (subdomain, custom domain, primary language, timezone, default currency, theme FK, soft-delete fields), `Organization` as thin payment-only entity per ADR-001 §3b (loose `Store.organization` grouping FK with **no config inheritance**; single default org via partial unique constraint; Themes/Shipping/Campaign templates are platform-level super-admin resources, never org-owned), and `StoreEmployee` (User × Store, `permissions_json` for the 18-module Full/Limited/No matrix, `full_access` master flag, invite flow, `last_login_at`, unique `(user, store)`). Enforcement UI for the matrix ships in TICKET-047; here only the storage + gating of `/admin/` module access by `permissions_json`.
**Acceptance criteria references:** AC-001, AC-002, AC-003, AC-004, AC-006, AC-201, AC-202
**PENDING blockers:** AF-C1 **DECIDED (human, 2026-07-11, per ADR-033): per-store matrix**; Orders "Limited access" semantics **DECIDED (human, 2026-07-11, per ADR-033): view-only**; delete-store retention semantics (11 Part C) remains open.

### TICKET-003-MEDIA: Media storage architecture
**Phase:** 1
**Priority:** P1
**Area:** Media/Images
**Depends on:** TICKET-001 (Django setup)
**ADRs referenced:** ADR-001
**Spec files:** 02, 04
**Estimated complexity:** M
**Description:** Define and implement the media storage backend (local filesystem for dev, configurable S3-compatible for production). Implement `ProductImage` model (`product` FK, `url`, `alt_text`, `display_order`, `is_primary`). Implement entity-image serving. No video upload — embed URL only (YouTube/Vimeo) — DECIDED AF-C2. Admin image upload UI for products.
**Acceptance criteria references:** AC-016, AC-020, AC-021
**PENDING blockers:** None.

### TICKET-003: Product + ProductVariant models
**Phase:** 1
**Priority:** P1
**Area:** 2. Product Catalog & Variants / 3. Inventory
**Depends on:** TICKET-002
**ADRs referenced:** ADR-001, ADR-005
**Spec files:** 02, 03, 04
**Estimated complexity:** L
**Description:** `Product` and `ProductVariant` (per-variant SKU unique per store, price/compare-at, weight, per-variant **inventory mode**: always-SOLD-OUT / no-tracking / fixed / fake-server-assigned / presale / ask-when-available / quotation — with the storefront CTA matrix per mode), video **embed URL only** field with YouTube/Vimeo validation (DECIDED AF-C2), and slugs via the shared NFD-normalizer from TICKET-001. Emit a `pre_save` slug-change signal on every slug-bearing model; the redirect table consuming it lands in TICKET-016 (signal contract defined here so no forward dependency). Pre-orders charge full price at placement (DECIDED FM-C3).
**Acceptance criteria references:** AC-010, AC-011, AC-012, AC-013, AC-015, AC-016, AC-020–AC-027
**PENDING blockers:** "erase inventory if checked" per-variant toggle semantics (11 Part C); quotation→order conversion flow (FM-C3 remainder — quotation mode ships form-only, conversion stubbed).

### TICKET-004: Product admin screens (store admin /admin/)
**Phase:** 1
**Priority:** P1
**Area:** 2. Product Catalog & Variants
**Depends on:** TICKET-003
**ADRs referenced:** ADR-001, ADR-005
**Spec files:** 02, 04
**Estimated complexity:** M
**Description:** Store-admin product list (search, status filters, bulk actions) and product editor (variants grid, per-variant inventory mode, images, video embed URL, SEO fields, slug edit with the >7-days-published confirmation step per ADR-005 §6). All querysets via `.for_store()`; module access gated by the TICKET-002 permission matrix.
**Acceptance criteria references:** AC-010, AC-012, AC-015, AC-016, AC-001
**PENDING blockers:** same as TICKET-003 (inventory-erase toggle semantics).

### TICKET-005: Collection models — smart rules + manual ordering
**Phase:** 1
**Priority:** P1
**Area:** 4. Collections
**Depends on:** TICKET-003
**ADRs referenced:** ADR-001
**Spec files:** 02, 03, 04
**Estimated complexity:** M
**Description:** `Collection` (smart vs manual), smart-rule engine (title/price/tag conditions, re-evaluation on product save + nightly job), and `CollectionProduct` through-model with **stable ordering keys, never positional indexes** (§XV-2 — deleting a product must not renumber others). Empty auto-collections return 404 on storefront (AC-032), 1–2-product collections are noindex (SEO rule, consumed by TICKET-025).
**Acceptance criteria references:** AC-030, AC-031, AC-032, AC-034
**PENDING blockers:** collection "product price" rule vs multi-currency/sale price (11 Part B — v1 matches store-default-currency base price, marked).

### TICKET-006: Collection admin screens
**Phase:** 1
**Priority:** P1
**Area:** 4. Collections
**Depends on:** TICKET-005
**ADRs referenced:** ADR-001
**Spec files:** 02, 04
**Estimated complexity:** S
**Description:** Collection list + editor: smart-rule builder UI, manual drag-ordering (writes stable ordering keys), product membership preview, publish toggle.
**Acceptance criteria references:** AC-030, AC-034
**PENDING blockers:** none beyond TICKET-005's.

### TICKET-007: Unified DiscountCode + GiftCardTransaction models
**Phase:** 1
**Priority:** P1
**Area:** 5. Pricing, Coupons & Gift Cards
**Depends on:** TICKET-002, TICKET-003
**ADRs referenced:** ADR-002
**Spec files:** 02, 03, 04
**Estimated complexity:** M
**Description:** Single `DiscountCode` model (type-discriminated: 4 coupon types + 2 gift-card types, `provenance`, `rules_json`, `per_email_limit`, `max_uses`/`uses_remaining`, case-insensitive per-store unique code) and append-only `GiftCardTransaction` ledger with denormalized `current_balance` maintained in the same transaction (ADR-002 §1–2). Include the atomic-decrement raw UPDATE and `SELECT FOR UPDATE` redemption helpers (ADR-002 §4) and the idempotent `CampaignReward` unique-constraint pattern (§5) — these are model-layer primitives; the service orchestration is TICKET-008. Migration scope also includes: **`DiscountCodeEmailUse` table** (`discount_code` FK, `email` str, `use_count` int; unique on `(discount_code, email)`), implementing the per-email limit `ON CONFLICT … DO UPDATE … WHERE use_count < per_email_limit` pattern described in ADR-002 §4.
**Acceptance criteria references:** AC-041, AC-042, AC-043, AC-044, AC-045
**PENDING blockers:** gift-card value modes beyond fixed $ (11:#3 remainder — kept out of v1 types); jurisdictional expiry (11:#3); UF-K refund-to-gift-card policy (ledger absorbs any option — do not block); `rules_json` vocabulary (11 Part C — v1: product ids, collection ids, min total).

### TICKET-008: Discount engine service
**Phase:** 1
**Priority:** P1
**Area:** 5. Pricing, Coupons & Gift Cards
**Depends on:** TICKET-007
**ADRs referenced:** ADR-002
**Spec files:** 02, 03, 08
**Estimated complexity:** M
**Description:** The single discount-resolution service (§XV-4): validates applicability at cart-apply (advisory) and **re-validates inside the checkout transaction**; applies **coupon first, then one gift card on the remainder** (DECIDED stacking order + UF-H); consumes at payment authorization with atomic counters and per-email `ON CONFLICT` counter rows; restores counters on released authorization in the same webhook transaction (ADR-002 §4–6).
**Acceptance criteria references:** AC-040–AC-050
**PENDING blockers:** timer-promo × coupon precedence (UF-A MEDIUM — service exposes a precedence hook, default coupon-wins, marked).

### TICKET-009: Order + OrderItem + OrderFulfillment models
**Phase:** 1
**Priority:** P1
**Area:** 6. Orders & Fulfillment
**Depends on:** TICKET-003, TICKET-007
**ADRs referenced:** ADR-001, ADR-002, ADR-004
**Spec files:** 02, 03, 04
**Estimated complexity:** M
**Description:** Order model with **explicit two-axis state machine** (payment status × fulfillment status, no state inferred from absence — §XV-3), OrderItem snapshot lines (price, discount lines, product-name copy), OrderFulfillment (tracking number, carrier, per-item), first-touch attribution columns (`utm_*`, `first_referrer`, `landing_page` — filled by TICKET-012's session capture), and checkout idempotency key (UF-I). Tax-free by design: prices tax-inclusive, no tax lines (DECIDED 11:#6).
**Acceptance criteria references:** AC-060, AC-061, AC-064
**PENDING blockers:** ~~canonical payment/fulfillment status enumerations (11 Part A CRITICAL)~~ RESOLVED (2026-07-02): Payment status: PENDING / PAID / PARTIALLY_REFUNDED / REFUNDED / FAILED / CANCELLED. Fulfillment status: NOT_SENT / SENT_TO_FULFILLMENT / PARTIALLY_SHIPPED / SHIPPED. Migration can be frozen.

### TICKET-010-CART: Cart and checkout service
**Implementation note: despite appearing at position 010, this ticket must be sequenced AFTER TICKET-018 (Stripe connector), which is its declared dependency.**
**Phase:** 1
**Priority:** P1
**Area:** Cart & Checkout
**Depends on:** TICKET-007 (DiscountCode), TICKET-009 (Order models), TICKET-018 (Stripe connector)
**ADRs referenced:** ADR-002, ADR-006, ADR-007
**Spec files:** 02, 03, 04, 08
**Estimated complexity:** L
**Description:** Implement `Cart` and `CartItem` models (30-day session persistence — DECIDED UF-E, guest identity by email, cart merge on account creation). Implement checkout orchestration service: address capture, discount application (coupon-first/gift-card-second service from TICKET-007/TICKET-008), payment authorization via Stripe/PayPal connector, order creation with UF-I idempotency key (unique per session + cart fingerprint, prevents double-submit), capture-window flag set for post-purchase upsell eligibility (ADR-007).
**Acceptance criteria references:** AC-060, AC-061, AC-040
**PENDING blockers:** None (all decisions made).

### TICKET-010: Order admin screens
**Phase:** 1
**Priority:** P1
**Area:** 6. Orders & Fulfillment
**Depends on:** TICKET-009
**ADRs referenced:** ADR-001
**Spec files:** 02, 04
**Estimated complexity:** M
**Description:** Order list (period filters, status filters, **product-name keyword search**), order detail (state transitions, refund entry, discount/gift-card lines), **CSV export**, **CSV tracking import** (order_id + tracking_number + carrier bulk-mark-shipped) and **manual tracking entry** — modes 1 and 2 of DECIDED AF-C3 (mode 3, FBA auto-pull, is TICKET-036).
**Acceptance criteria references:** AC-060, AC-061, AC-062, AC-065, AC-005
**PENDING blockers:** none — Orders "Limited access" semantics for employees **DECIDED (human, 2026-07-11, per ADR-033): view-only** (carried from TICKET-002).

### TICKET-011: Customer model + guest-checkout identity
**Phase:** 1
**Priority:** P1
**Area:** 7. Customers
**Depends on:** TICKET-009
**ADRs referenced:** ADR-001, ADR-002
**Spec files:** 02, 03
**Estimated complexity:** S
**Description:** Email-keyed customer identity (DECIDED: guest checkout + optional account creation post-purchase). Customer row auto-created/merged by order email; per-email coupon limits and verified-buyer badge both key on this email. Tags field (consumed by lead capture, TICKET-034).
**Acceptance criteria references:** AC-045, AC-123
**PENDING blockers:** PII retention/anonymization/erasure policy (GDPR, 11 Part B — model includes `anonymized_at` field now; policy wiring later).

### TICKET-012: Analytics event pipeline — separate DB, async ingest, FiredPixel dedup
**Phase:** 1
**Priority:** P1
**Area:** 8. Analytics & Reports / 21. Pixels
**Depends on:** TICKET-002
**ADRs referenced:** ADR-004 (revised: 500+ orders/day/store)
**Spec files:** 02, 03, 08
**Estimated complexity:** L
**Description:** Provision the **separate analytics database** (plain Postgres with monthly partitioning day 1; schema TimescaleDB-compatible for TICKET-044), `Event` table (closed enum v1, soft IDs, no FKs), inlined beacon JS batching to the ingest endpoint, **async ORM-free ingestion** (Celery batch task or direct insert; raw SQL/asyncpg, dropped-batch counter + dead-letter queue), server-side `purchase` event at payment confirmation, first-touch UTM session capture copied onto Order, `FiredPixel` dedup table with **insert-before-fire**, purge policy (default 90 days, partition drops), and the nightly purchase-vs-order mismatch detector (§XV-1).
**Acceptance criteria references:** AC-110, AC-111, AC-112, AC-082
**PENDING blockers:** none blocking build (event vocabulary designed to support all candidate column definitions per ADR-004 §2).

### TICKET-013: Analytics reports (13 report types)
**Phase:** 1
**Priority:** P1
**Area:** 8. Analytics & Reports
**Depends on:** TICKET-012, TICKET-009
**ADRs referenced:** ADR-004
**Spec files:** 02, 04, 08
**Estimated complexity:** L
**Description:** The 13 report types (8 sales + 4 traffic + dashboard) reading **the analytics DB only** (ADR-004 §3c; display names resolved by ID in the presentation layer). New columns: collection display/CTR/scroll-%/purchase-rate, **customer-local-hour bucketing** (denormalized at ingest), state-level hierarchy for US/CN/RU/IN. Money metrics per DECIDED AF-C5 (Gross = pre-refund order totals; Net = Gross − Refunds; normalize the stray "Net sales" label). Migration scope also includes: **`AggregatedMetric` table** (analytics DB; fields: `metric_type`, `store_id`, `period_start`, `period_end`, `dimension_key`, `value`). Nightly aggregation job reads raw events, writes `AggregatedMetric` rows, and purges raw events older than the retention window (partition/chunk drops — never row-level DELETE). Aggregation jobs scheduled via TICKET-014 job infra once available; plain Celery beat until then.
**Acceptance criteria references:** AC-080, AC-081, AC-082, AC-083, AC-084, AC-085, AC-035
**PENDING blockers:** final denominators for remaining owner columns (11 Part A CRITICAL); attribution window / first-vs-last override; multi-timezone country bucketing refinement; full state-report country list.

### TICKET-014: AI Job System — models, registry, heartbeat reclaim
**Phase:** 1
**Priority:** P1
**Area:** 24. AI Job System
**Depends on:** TICKET-002
**ADRs referenced:** ADR-003
**Spec files:** 02, 04, 08
**Estimated complexity:** L
**Description:** `AiJob`, `AiJobRun`, `AiJobDependency` (DAG, phase-2 jobs as separate jobs — §VIII), `AiJobOutput` (stable field IDs, append-on-duplicate), `AiJobValidation`, `AiJobMetric`; explicit status enum `NOT_STARTED/IN_PROGRESS/DONE/ERROR/SKIPPED`; job-type **registry/plugin pattern** (capabilities, `build_prompt`, `validate_output`, `persist_output`, `estimate_tokens`); scheduler loop with **heartbeat reclaim** (dead > 3× interval ⇒ `ERROR(dead)` + re-queue); idempotent step-skip design; terminal runners preferred with API fallback per **DECIDED FM-C2** (simple monthly token/credit quota per store, configured in super-admin); human-review queue gate.
**Acceptance criteria references:** AC-222, AC-223, AC-224
**PENDING blockers:** none blocking (FM-C2 credits model DECIDED — monthly quota per store).

### TICKET-015: AI output validation service
**Phase:** 1
**Priority:** P1
**Area:** 24. AI Job System
**Depends on:** TICKET-014
**ADRs referenced:** ADR-003
**Spec files:** 02, 08
**Estimated complexity:** M
**Description:** The persistence gate per ADR-003 §6 and design-pattern-ideas §I: chunk assembly in emit order, continuation-suffix stripping before **field-ID dedup**, bracket/token balance checks, **length-ratio gate** (≥35% of source once source > ~200 chars, CJK-safe), API-path `stop_reason == "end_turn"` **assertion** (`max_tokens` ⇒ ERROR), all-fields-or-nothing commit. Every check writes an `AiJobValidation` row pass or fail.
**Acceptance criteria references:** AC-220, AC-221
**PENDING blockers:** none.

### TICKET-016: URL resolution + Permalink model
**Phase:** 1
**Priority:** P1
**Area:** 9. SEO / 16. Redirects / 10. Multilingual
**Depends on:** TICKET-003, TICKET-005
**ADRs referenced:** ADR-005
**Spec files:** 02, 03, 07, 08
**Estimated complexity:** L
**Description:** `PermalinkTranslation` (URL-owning slug rows, unique `(store, lang, slug)`), **`PermalinkResolver`** as the only internal-URL builder (template tag, per-store+epoch cache), and `PermalinkRedirect` with **`pre_save` auto-301 creation** (consuming TICKET-003's signal), `none` type for dead URLs, **chain collapse at write time** (A→B + B→C ⇒ A→C; loop rejection), and **no manual redirect creation in store admin** (view/manage only — DECIDED). Cache invalidation shares the redirect-writing signal path.
**Acceptance criteria references:** AC-013, AC-014, AC-090, AC-093, AC-094, AC-095, AC-101, AC-102, AC-190, AC-191, AC-192
**PENDING blockers:** ~~domain→language→country model (11:#9 MEDIUM)~~ RESOLVED (2026-07-03): **ADR-008** (`adr_008_multilingual_domains.md`). Scope additions from ADR-008 §5: `StoreDomain`/`StoreLanguage`/`ShippingCountry` models + constraints, `PLATFORM_APEX_DOMAIN` setting + data migration, `resolve_locale` + locale middleware, absolute-URL `base_url` composition in `PermalinkResolver`, ISO-lang-code reserved-slug rule, disabled-language 410.

### TICKET-017: Payment Control Plane models + /superadmin/ routing screens
**Phase:** 1
**Priority:** P1
**Area:** 23. Payment Control Plane / 22. Payment processing
**Depends on:** TICKET-002
**ADRs referenced:** ADR-006, ADR-001
**Spec files:** 02, 04, 06
**Estimated complexity:** L
**Description:** `ProcessorAccount` (vault:// credential references, health-status machine, manual block), `OrganizationRule` (stage1 geography / stage2 allocation, `conditions_json`/`outcome_json`, self-referential `fallback_rule`, priority-within-stage, save-time broad-shadows-narrow lint), `OrgPool`, `ProcessorSplitCounter`, `DecisionLog` (rule IDs + outcomes, no buyer PII), `PaymentMethod` + `ProcessorOption` (backup_chain / alternate_evenly), all platform-global under `/superadmin/`. **Live-on-save** (DECIDED 11:#4) — no versioning; DecisionLog is the audit trail.
**Acceptance criteria references:** AC-130, AC-134, AC-203
**PENDING blockers:** ~~CRITICAL vault backend~~ RESOLVED (2026-07-02): django-fernet-fields, FERNET_KEY env var. See Settings-C3 in `11_uncertainties_to_validate.md`. Remaining: health source probes-vs-manual (Part E); org-threshold accounting (what counts, month boundary, currency conversion); DecisionLog retention; simulation-page content.

### TICKET-018: Stripe connector
**Phase:** 1
**Priority:** P1
**Area:** 22. Payment processing
**Depends on:** TICKET-017
**ADRs referenced:** ADR-006, ADR-007
**Spec files:** 02, 03, 06
**Estimated complexity:** M
**Description:** Define the connector registry interface (authorize, capture, refund, health-probe, `supports_delayed_capture` flag) and the Stripe implementation: **authorize + delayed capture** (required by the upsell capture-window, ADR-007), webhook handling (idempotent, counter-restore on released auth per ADR-002 §4), health check. Day-1 processor per DECIDED 11:#7.
**Acceptance criteria references:** AC-132, AC-061
**PENDING blockers:** per-processor settings screen placement/content (11:#7 remainder — API keys, test mode, webhooks UI; connector itself unblocked).

### TICKET-019: PayPal connector
**Phase:** 1
**Priority:** P1
**Area:** 22. Payment processing
**Depends on:** TICKET-018
**ADRs referenced:** ADR-006, ADR-007
**Spec files:** 02, 03, 06
**Estimated complexity:** M
**Description:** PayPal implementation of the exact connector interface from TICKET-018 (registry entry, no special-casing at call sites). Must **confirm and document delayed-capture support** (order auth → capture) — this determines PayPal-routed orders' eligibility for the post-purchase funnel (ADR-007 §2).
**Acceptance criteria references:** AC-132, AC-133
**PENDING blockers:** ~~PayPal delayed-capture capability confirmation~~ **RESOLVED (2026-07-05)** — confirmed: PayPal supports delayed capture via the Orders API `intent=AUTHORIZE` flow on standard business accounts. PayPal: configurable `paypal_capture_mode` on ProcessorAccount; choices: `delayed` (default, funnel-eligible) / `immediate` (skip funnel). Default is `delayed`.

### TICKET-020: Payment routing engine — decision tree evaluator
**Phase:** 1
**Priority:** P1
**Area:** 23. Payment Control Plane
**Depends on:** TICKET-017, TICKET-018, TICKET-019
**ADRs referenced:** ADR-006
**Spec files:** 02, 03, 04, 08
**Estimated complexity:** L
**Description:** The evaluator: stage-1 geography → Organization or OrgPool; stage-2 allocation (percent split via **running counter picking the member furthest behind target**, threshold switch vs monthly caps); explicit fallback chain; health check **at evaluation time** with automatic fall-through; exhaustion ⇒ default org with `over_cap=true` flag (DECIDED AF-C6 — never silently reject); method-level strategy composition; **DecisionLog write on every decision, no sampling**.
**Acceptance criteria references:** AC-130, AC-131, AC-132, AC-133, AC-134
**PENDING blockers:** org-level split × method-level strategy layer composition (Part E CRITICAL — two-counter design keeps layers independent; final composition rule needs sign-off); health source (Part E).

---

# Phase 2 — Key features

### TICKET-021: Abandoned-checkout campaign
**Implementation note: build TICKET-022 (email infrastructure) FIRST, then this ticket. Despite TICKET-022's higher number, TICKET-021 depends on TICKET-022.**
**Phase:** 2
**Priority:** P2
**Area:** 11. Abandoned Checkout
**Depends on:** TICKET-007, TICKET-009, TICKET-011, TICKET-022 (sender infrastructure)
**ADRs referenced:** ADR-007, ADR-002
**Spec files:** 02, 03, 04, 08
**Estimated complexity:** M
**Description:** Abandonment detection, `CampaignSession`-based per-step state (`abandoned → email_1_sent → … → converted`, never a single Order enum — ADR-007 §5), email wizard (Type/Style/Copy **+ send delay** — DECIDED AF-C4 2026-07-04), **resume-cart link with 30-day expiry = cart lifetime** (DECIDED UF-E; token scheme DECIDED 2026-07-05: HMAC-signed using `django.core.signing` — HMAC(cart_id + expiry timestamp) signed with Django `SECRET_KEY`; stateless, no extra DB row, same pattern as Django's password-reset links; forged and expired (> 30 days) tokens are both rejected with the same "link expired or invalid" message — no token-existence leak), suppression when the order completes, idempotent coupon auto-issue via `CampaignReward` (ADR-002 §5), recovery-revenue attribution.

**Send delay (DECIDED AF-C4, 2026-07-04):**
- Each email step carries a `send_delay_hours` field: **integer, required, default = 1**. A campaign email cannot be saved without a delay value.
- The field lives **inside the email wizard**, alongside Type/Style/Copy — not in a separate schedule step. UI: label **"Send after"**, a number input + a **hours/days unit dropdown** (days are stored as hours × 24; storage is always `send_delay_hours`).
- **Abandonment trigger:** a cart is "abandoned" when a customer adds items and does not complete checkout within the `send_delay_hours` window of the first email step.
- Per-email model fields: `send_delay_hours`, `email_type` (warning/reminder/incentive), `email_style`, `subject`, `body_template`, `coupon_template` (optional). Coupon type (DECIDED 2026-07-05): a **unique single-use `DiscountCode` generated per recipient via `CampaignIssuedCode`** (the attribution/idempotency link built in T027) — **not a shared fixed code**. The issued `DiscountCode` has `usage_limit=1`, `provenance='campaign'`.
- **Celery beat task** scans carts older than `send_delay_hours` that have not converted and enqueues the corresponding email send.
- **No re-trigger:** a cart is never re-triggered for a step if an email was already sent for that session (dedup key: cart session × campaign step; survives worker retries).
- ASSUMPTION (LOW): for steps 2..N, `send_delay_hours` is measured from the abandonment timestamp; the wizard warns when a later step's delay is ≤ an earlier step's.

**Acceptance criteria references:** AC-070–AC-075, AC-182
**PENDING blockers:** ~~AF-C4 send-delay field placement~~ **RESOLVED (2026-07-04)** — see Send delay block above. ~~Resume-token security~~ **RESOLVED (2026-07-05)** — HMAC-signed via `django.core.signing`, see Description. ~~`CODE123` literal vs generated unique coupons~~ **RESOLVED (2026-07-05)** — unique single-use `DiscountCode` per recipient via `CampaignIssuedCode`, see per-email model fields. Remaining: GDPR basis for emailing non-purchasers (11 Part B).

### TICKET-022: Transactional email system
**Phase:** 2
**Priority:** P2
**Area:** 12. Email marketing
**Depends on:** TICKET-002, TICKET-009
**ADRs referenced:** ADR-001
**Spec files:** 02, 04, 06, 08
**Estimated complexity:** M
**Description:** Shared platform sender `noreply@mail.pradize.com` (DECIDED Settings-C1 — store sets display name + reply-to only; single SPF/DKIM for `*.mail.pradize.com`), 6 system templates as **platform-level masters** (super-admin owned per ADR-001 §3b), Twig-style variable rendering, per-template enable, test-send. Order confirmation triggers on payment confirmed, not checkout start.
**Acceptance criteria references:** AC-180, AC-181, AC-182, AC-183
**PENDING blockers:** multilingual template model (per-language variants, fallback, retranslation jobs on master update — 11 Part D; single-language v1, structure ready for TICKET-024).

### TICKET-023: Review system
**Phase:** 2
**Priority:** P2
**Area:** 18. Reviews
**Depends on:** TICKET-009, TICKET-011, TICKET-014, TICKET-022
**ADRs referenced:** ADR-003, ADR-001
**Spec files:** 02, 03, 04, 08
**Estimated complexity:** M
**Description:** Review model with **provenance flag** (customer / AI-generated), moderation tabs, review-request email scheduler (**N days after shipment**, not order — AC-122/181), **verified-buyer badge via order-email match** (DECIDED checkout identity), customer-only display threshold, AI-generated-review job type registered in the TICKET-014 registry with human-review gate default ON.
**Acceptance criteria references:** AC-120, AC-121, AC-122, AC-123, AC-181
**PENDING blockers:** AI-review legal/compliance flags (11 Part B — provenance flag stored from day 1; publication policy needs human sign-off).

### TICKET-024: Multilingual content pipeline
**Phase:** 2
**Priority:** P2
**Area:** 10. Multilingual content
**Depends on:** TICKET-014, TICKET-015, TICKET-016
**ADRs referenced:** ADR-005, ADR-003
**Spec files:** 02, 03, 07, 08
**Estimated complexity:** L
**Description:** `ProductTranslation` (and collection/page equivalents) holding translated *text*; translated slugs live **only** in `PermalinkTranslation` (ADR-005 §2); translation job type (highest priority in the job queue, auto-publish default) with sub-job types (`full`, `slug_only`, chunked) and TICKET-015 validation; hreflang generation reads the resolver map; unpublished translation ⇒ 404 on the translated URL.
**Acceptance criteria references:** AC-100, AC-101, AC-102, AC-104, AC-091, AC-220, AC-221
**PENDING blockers:** ~~domain→language→country model (11:#9)~~ RESOLVED (2026-07-03): **ADR-008**. Scope addition from ADR-008 §3/§5: the merged General Settings + Domains admin screen (Domains table + per-language table with lang/domain/routing/enabled/target-countries/translation-coverage columns, Add Language modal per ML-010).

### TICKET-025: SEO metadata layer
**Phase:** 2
**Priority:** P2
**Area:** 9. SEO
**Depends on:** TICKET-016, TICKET-024
**ADRs referenced:** ADR-005
**Spec files:** 02, 07, 08
**Estimated complexity:** M
**Description:** Canonical, **hreflang, og:locale in ONE base-template block** (`{% block seo_head %}`, legal pages no-op override — §XI); hreflang set = live translations ∩ available-countries, cached per page; per-page-type quality gates (thin collections noindex, empty collections 404); duplicate-canonical prevention.
**Acceptance criteria references:** AC-090, AC-091, AC-092, AC-093, AC-033
**PENDING blockers:** none beyond TICKET-016's.

### TICKET-026: Sitemap + robots.txt generation
**Phase:** 2
**Priority:** P2
**Area:** 9. SEO
**Depends on:** TICKET-016, TICKET-024
**ADRs referenced:** ADR-005
**Spec files:** 02, 07
**Estimated complexity:** S
**Description:** Per-language sitemaps built from the PermalinkResolver map (A/B variant URLs excluded — DECIDED FM-C4; noindex pages excluded), robots.txt per store/domain, **event-driven debounced regeneration + nightly full rebuild**.
**Acceptance criteria references:** AC-091, AC-092, AC-093
**PENDING blockers:** none beyond TICKET-016's.

### TICKET-027: Up-sell campaign models + admin (5 archetypes)
**Status:** IMPLEMENTED (2026-07-04) — models, migrations, admin, validation layer, eligibility precedence, and the CampaignStepTranslation AiJob trigger are built per ADR-009 (incl. Appendix B).
**Phase:** 2
**Priority:** P2
**Area:** 13. Up-sell & conversion
**Depends on:** TICKET-007, TICKET-009, TICKET-020
**ADRs referenced:** ADR-007, ADR-002, ADR-006
**Spec files:** 02, 03, 04, 08
**Estimated complexity:** L
**Description:** `Campaign` + ordered `CampaignStep` rows with **stable-FK accept/decline branches** (never positional — §VII), `CampaignSession` state machine (`NOT_STARTED/IMPRESSION/INTERACTION/CONVERTED/DISMISSED/EXPIRED`, unique per (order, campaign_type)), 5 archetypes (related-products, buy-X-get-X, storewide discount, one-click funnel, order bump), eligibility/exclusion evaluation at funnel entry, shared rule-builder targeting. Admin builds single-offer funnels here; the visual multi-step builder is TICKET-039.

**Campaign-type rules (DECIDED 2026-07-04):**
1. **One-click post-purchase upsell** — capture-window model (decided earlier, ADR-007): shown on the thank-you page; an accept is charged inside the payment capture window (payment flow itself is TICKET-028).
2. **Buy-X-get-X free** — adds a free item when the qualifying item is in the cart. **No free-value cap**: the free item is always 100% free regardless of its price. The "Maximum value of free items" toggle seen in screenshot super-admin-11-04 is dropped (recorded in 12_out_of_scope.md).
3. **Storewide discount for next order** — generates a **single-use** discount code with **configurable expiry, default 30 days**; campaign settings field: **"Discount code expires after N days"**. The code is emailed to the customer and/or shown on the thank-you page.

**Precedence rule (DECIDED 2026-07-04): store-admin wins.**
- When a product has both a super-admin campaign and a store-admin campaign, the **store-admin campaign takes precedence at runtime**.
- Super-admin campaigns are visible to store-admins but **read-only** from the store-admin view.
- If only a super-admin campaign exists for a product, it applies.

**Model outline (as implemented — ADR-009):**
- `Campaign` (StoreOwnedModel, **non-nullable** store FK): type, status (active/paused/draft), targeting rules; **`owner_scope`** discriminator (store/platform — not `owner_level`, not a nullable store): platform (super-admin) campaigns are `owner_scope='platform'` rows pinned to a store, read-only in the store admin; precedence resolved in `evaluate_eligibility` only. **`entry_step`** nullable FK (SET_NULL) marks the funnel entry — required to activate, never derived from position.
- `CampaignStep` (multi-step funnel, DECIDED UF-J; the 2026-07-04 decision notes call this "CampaignNode" — same entity, `CampaignStep` name retained, ASSUMPTION LOW): position (display hint only), offer content, stable accept/decline branch FKs.
- `CampaignIssuedCode` (replaces the earlier `CampaignCode` sketch — ADR-009 §3): the next-order code itself is a real `DiscountCode` (single-use `usage_limit=1`, `ends_at` from the campaign's "expires after N days" setting, default 30, `provenance='campaign'`); `CampaignIssuedCode` is the thin attribution + idempotency link (CampaignSession, CampaignStep) → DiscountCode, unique per (campaign_session, step).
- `CampaignStepTranslation` (**approved addition 2026-07-04** — ADR-009 Appendix B): per-language translated step content (`title`, `description`, `cta_label`), `status` draft/published, `ai_job` FK nullable (SET_NULL), unique (store, step, lang_code), `clean()` enforces store == step.campaign.store. AiJob trigger on `CampaignStep` post_save creates `campaign_step_translation` jobs (priority 100) via the AiJob CLI runner — never direct API.

**Acceptance criteria references:** AC-140, AC-141, AC-142, AC-144, AC-146 (precedence)
**PENDING blockers:** ~~Safety Agent review gate~~ **RESOLVED (2026-07-03)** — review blocked T028 (payment flow), not this ticket's data model; T028 now cleared. ~~Super-admin vs store campaign precedence~~ **RESOLVED (2026-07-04)**: store-admin wins. ~~Buy-X-get-X cap × stacking~~ **RESOLVED (2026-07-04)**: no free-value cap (stacking toggle unchanged). ~~Storewide-discount next-order constraints~~ **RESOLVED (2026-07-04)**: single-use + configurable expiry (default 30 days). No PENDING blockers remain on this ticket.

### TICKET-028: Up-sell capture-window payment flow
**Phase:** 2
**Priority:** P2
**Area:** 13. Up-sell & conversion / 22. Payments
**Depends on:** TICKET-010-CART (for `setup_future_usage` at checkout), TICKET-018, TICKET-019, TICKET-020, TICKET-027
**ADRs referenced:** ADR-007, ADR-006
**Spec files:** 02, 03, 08
**Estimated complexity:** M
**Description:** Checkout **authorizes** (no capture) and saves the payment method off-session (`setup_future_usage=off_session` via Stripe, or PayPal vault equivalent from TICKET-010-CART scope); thank-you page shows funnel steps while `now < capture_window_expires_at` (default 10 min, per campaign).

Implementation scope includes all of the following:

- **Two-charge baseline:** at accept, capture the original auth at the original amount, then charge the upsell as a separate off-session payment (new PaymentIntent, `off_session: true`, `confirm: true`) using the vaulted payment method. A failed upsell charge degrades softly: original order intact, upsell item removed/marked failed, no error page for the shopper (AC-141 edge case).
- **OrderCharge table (migration):** one row per processor charge (original capture and each upsell step); fields: `order` FK, `processor_charge_id` (unique), `amount`, `captured_at`, `refunded_amount` (default 0).
- **CampaignSessionToken table (migration):** fields: `campaign_session` FK, `purpose` (enum: `thankyou-view` / `upsell-act`), `token_hash` (unique), `expires_at`, `used_at` (nullable). Single-use for `upsell-act`; long-lived for `thankyou-view`. Token value stored as a hash; raw value returned only on creation.
- **Signed thank-you tokens:** generate a `thankyou-view` token (long-lived, ~72 h, sent in confirmation email) and a `upsell-act` token (short-lived, lifetime = capture window, single-use, embedded in the thank-you page DOM). POST-only accept/decline endpoints, CSRF-protected, rate-limited (3 attempts per order, then auto-decline). Opaque order IDs in URLs. Tokens excluded from logs, analytics, and `DecisionLog`.
- **Provisional item state:** write upsell items as `PENDING_CAPTURE`; promote to confirmed on charge webhook success. Fulfillment triggers and confirmation emails must ignore provisional items.
- **Processor account pinning:** persist `ProcessorAccount` FK on `Order` at authorization; all follow-up operations (capture, upsell charge, watchdog, refunds) assert against this stored account; routing engine never re-invoked; upsell charge does not increment `ProcessorSplitCounter`.
- **Atomic race guard:** accept, decline, and watchdog all use `UPDATE … WHERE state='AUTHORIZED' AND affected_rows == 1`; loser aborts without a processor call. Idempotency keys: `capture:{order_id}` for original, `upsell:{order_id}:{step_id}` for each upsell step. Window check inside the locked transaction (DB `NOW()`) before any processor call.
- **Watchdog:** covers (a) `AUTHORIZED` past window → capture original, void all `PENDING_CAPTURE` items; (b) stuck `CAPTURE_IN_PROGRESS` → reconcile with processor first, then proceed as (a) if genuinely uncaptured. Schedule tighter than auth validity window; alert on any order uncaptured past window + grace.
- **Decline/expiry:** capture original only; void/remove all `PENDING_CAPTURE` items; session → `DISMISSED` / `EXPIRED`.
- Eligibility consults the DecisionLog's chosen processor for `supports_delayed_capture`; pre-orders excluded (full charge at placement, FM-C3).

**Acceptance criteria references:** AC-141, AC-142, AC-143, AC-145
**PENDING blockers:** Safety review **RESOLVED (2026-07-03)** — all 9 amendments applied to ADR-007 and this ticket. T028 is cleared for implementation after T027. Dependency status (2026-07-04): T027 models are now fully specced (precedence, buy-X-get-X no-cap, `CampaignCode` single-use/expiry all DECIDED). ~~PayPal delayed-capture confirmation from TICKET-019~~ **RESOLVED (2026-07-05)** — PayPal supports delayed capture (Orders API `intent=AUTHORIZE`, standard business accounts). PayPal: configurable `paypal_capture_mode` on ProcessorAccount; choices: `delayed` (default, funnel-eligible) / `immediate` (skip funnel). Default is `delayed`. Accounts set to `immediate` route their orders straight to fulfillment (funnel-ineligible), consistent with the `supports_delayed_capture` eligibility check in the description. No PENDING blockers remain on this ticket.

### TICKET-029: Storefront theme system (3 curated themes)
**Phase:** 2
**Priority:** P1
**Area:** 25. Themes / storefront
**Depends on:** TICKET-003, TICKET-005, TICKET-016
**ADRs referenced:** ADR-001, ADR-005
**Spec files:** 02, 03, 06, 07
**Estimated complexity:** XL
**Description:** Twig(-style) template engine integration, theme library **owned by platform super-admin** (ADR-001 §3b), per-store active theme + light customization (fonts, colors, images), and the 3 curated themes (foods / fashion / general mid-high-end) covering all storefront pages (collection, product incl. A/B variant, cart/floating cart, checkout, thank-you with funnel slot, search, 404, contact/quotation — DECIDED FM-C1b scope). All URLs via the resolver template tag; SEO block from TICKET-025 slot-compatible.
**Acceptance criteria references:** AC-017, AC-027, AC-032, AC-090, AC-100
**PENDING blockers:** **11:#11 (Designer theme proposals PENDING)** and FM-C1b storefront designs — engine + one reference theme can proceed; the 3 curated designs block on the Designer Agent.

### TICKET-030: Pixel integrations
**Phase:** 2
**Priority:** P2
**Area:** 21. Pixels & Analytics integrations
**Depends on:** TICKET-012, TICKET-009, TICKET-029
**ADRs referenced:** ADR-004
**Spec files:** 02, 03, 06, 08
**Estimated complexity:** M
**Description:** Facebook, Google Analytics (Enhanced Ecommerce), Snapchat, Pinterest, TikTok pixel emitters driven by the same event stream; **FiredPixel insert-before-fire dedup** (TICKET-012 table); Purchase fires on payment confirmation only (never at intent creation, never re-fired on thank-you reload); UTM first-touch session capture already provided by TICKET-012.
**Acceptance criteria references:** AC-110, AC-111, AC-112
**PENDING blockers:** exact ecommerce event/parameter mapping per provider (11 Part C — value, currency fields; propose mapping table for sign-off).

### TICKET-031: Currency display converter (storefront)
**Phase:** 2
**Priority:** P2
**Area:** 17. Currency conversion
**Depends on:** TICKET-003, TICKET-029
**ADRs referenced:** ADR-001, ADR-015, ADR-023
**Spec files:** 02, 03, 06, 08
**Estimated complexity:** S
**Description:** **Display-only** conversion (transaction always charged in store default currency — AC-161) across 34 currencies with per-currency symbol/code/placement and consistent rounding honoring .99 psychological pricing. Currency data is platform-global. Per ADR-023: single `display_amount()` resolver + `apply_rounding()` (ceil-minus-smallest-unit, zero-amount guard); shopper selection via header picker persisted in `session['display_currency']`, written only by the explicit `POST /currency/set/` endpoint (never by middleware — deliberately avoids the ADR-015 sf_lang unconditional-write bug class); converts product/collection/cart pages only — checkout/thank-you/order/emails stay in `store.default_currency` per the already-shipped ML-004 (ADR-015), which supersedes AC-160's "checkout subtotals" wording.
**Acceptance criteria references:** AC-160, AC-161, AC-162
**PENDING blockers:** ~~rate source/frequency and rounding rules (11 Part B — resolved together with TICKET-037)~~ **RESOLVED (2026-07-10, ADR-023)** — manual rate entry always authoritative + optional ECB daily-feed auto-refresh (off by default, zero new dependencies); `.99` rounding via `ceil(raw) − smallest_unit`. No PENDING blockers remain on this ticket; two low-priority product decisions (exact 3 non-ECB currencies to reach 34; 0-decimal-currency rounding convention) are tracked in `11_uncertainties_to_validate.md` and do not block implementation.

### TICKET-032: Shipping zones + carriers
**Phase:** 2
**Priority:** P1
**Area:** 19. Shipping
**Depends on:** TICKET-002, TICKET-003, TICKET-009
**ADRs referenced:** ADR-001
**Spec files:** 02, 04, 06, 08
**Estimated complexity:** M
**Description:** Zone model (**platform-level super-admin owned**, ADR-001 §3b), most-specific-zone-over-"All Countries" matching precedence, weight-range exception rules, carrier registry with tracking-URL templates. Free-shipping coupon integration via TICKET-008.
**Acceptance criteria references:** AC-170, AC-171, AC-172, AC-173, AC-050
**PENDING blockers:** "Model 1/Model 2" shipping models + per-product override semantics (11 Part D); exception rule-type list.

### TICKET-033: Catalog feeds
**Phase:** 2
**Priority:** P2
**Area:** 20. Catalog feeds
**Depends on:** TICKET-003, TICKET-005, TICKET-016
**ADRs referenced:** ADR-026 (primary — full design, ACCEPTED human 2026-07-11), ADR-005/ADR-008 (permalink resolver + feed matrix source), ADR-022 D4 (frozen `catalog_item_id`, drift-test NON-NEGOTIABLE), ADR-023/FEED-C1 (`display_amount()` currency conversion)
**Spec files:** 02, 07, 08; `docs/adr/ADR-026-catalog-feeds.md`
**Estimated complexity:** M
**Description:** New `feeds` app: `FeedProvider` registry (`google`/`facebook`, Pinterest deferred per P-1), one RSS 2.0/`g:`-namespace renderer, **one item per active variant** (`id = pixels.events.catalog_item_id`, `item_group_id = product.pk`), prices via `display_amount()` in the feed country's display currency (new `COUNTRY_TO_CURRENCY` map + `feed_display_currency()`, loud `ERROR` on missing rate), links via PermalinkResolver (canonical primary only), **pre-generated** files (30-min debounced beat + nightly rebuild, temp + `os.replace`), explicit `FeedTarget` status machine with persisted per-product exclusion reasons (AC-211/AC-212), per-store rotatable token URLs (`hmac.compare_digest`, AC-213), `"feeds"` added to `RESERVED_TOP_LEVEL_SLUGS`. **Two Developer tickets per the ADR's split (ADR-026 D9): T033-A (feeds app, models/migrations, registry + providers, currency map, item builder, renderer, signals, beat/nightly tasks, feed_view + URL registration, drift tests) and T033-B (admin cards, status page + exclusions, regenerate button, token rotation, launch-readiness warning — depends on T033-A).** Mandatory Safety Agent gate (public token-protected endpoint) and SEO Agent gate (canonical-link rule, robots) before release.
**Acceptance criteria references:** AC-210, AC-211, AC-212, AC-213; ADR-026 "Tests required" (drift tests, FEED rules, engine behaviour)
**PENDING blockers:** none. Feed cadence (formerly Part C MEDIUM) is DECIDED (ADR-026 P-9: 30-min debounced beat + nightly rebuild, both settings-overridable). Nine product-level PENDING items (P-1…P-9) were approved as their recommended (mostly "defer") defaults on 2026-07-11 — see `11_uncertainties_to_validate.md` — none blocks this ticket.

### TICKET-034: Lead capture overlay
**Phase:** 2
**Priority:** P2
**Area:** 14. Lead capture & notifications
**Depends on:** TICKET-007, TICKET-011, TICKET-029
**ADRs referenced:** ADR-027 (primary — full design, ACCEPTED human 2026-07-11), ADR-002 (§4 atomic counters, §5 `CampaignReward`), ADR-012 (slot registry), ADR-018 (antispam reuse)
**Spec files:** 02, 03, 04, 08; `docs/adr/ADR-027-conversion-overlays.md`
**Estimated complexity:** M
**Description:** Overlay with display/go-to-URL variants, exit-intent trigger with mobile/tablet fallback (time trigger), page exclusion list, per-session frequency cap, customer tagging on signup, **idempotent coupon issue on signup** (`CampaignReward` pattern, ADR-002 §5). Design per ADR-027 D1–D5/D9/D10: new `engagement` app, `LeadCaptureCampaign` + `LeadSignup` (consent proof, no IP) + `LeadCaptureCampaignTranslation`, antispam-reused signup POST, shared-coupon issuance, `F()` counters (conversion rate = conversions/impressions), overlay theme registry, token styling. **Mandatory Safety Agent gate before release** (public unauthenticated POST pair).
**Acceptance criteria references:** AC-150, AC-151, AC-152, AC-153, AC-154; ADR-027 "Tests required" (T034 list)
**PENDING blockers:** none blocking implementation. Double opt-in per target market (ADR-027 D3) is now **DECIDED (human, 2026-07-11): enabled by default for stores targeting Germany, single opt-in elsewhere** — must be live **before the first marketing send to captured leads**, not before this ticket ships; final legal sufficiency remains tracked as an external (non-blocking) item on the extended ADR-025 legal-sign-off track (`11_uncertainties_to_validate.md`).

### TICKET-035: Recent-purchase notification (social proof)
**Phase:** 2
**Priority:** P3
**Area:** 14. Lead capture & notifications
**Depends on:** TICKET-009, TICKET-029, TICKET-034 (app skeleton only — `engagement` app created there)
**ADRs referenced:** ADR-027 (primary — full design, ACCEPTED human 2026-07-11), ADR-004 (no-IP invariant), ADR-012 (slot registry)
**Spec files:** 02, 03, 06; `docs/adr/ADR-027-conversion-overlays.md`
**Estimated complexity:** S
**Description:** Storefront popup showing recent real purchases from a configurable rolling window; **GDPR-safe buyer display** (first name + coarse location at most, configurable to fully anonymous). Design per ADR-027 D6–D8: `SocialProofSettings` (default OFF, `display_mode=anonymous` default), PAID orders only, server-embedded sanitized JSON (60 s cache, **no public endpoint**), coarse time buckets, **never fabricate** (empty window → renders nothing). **Mandatory Safety Agent gate before release** (buyer-derived data published on every storefront page).
**Acceptance criteria references:** ADR-027 "Tests required" (T035 list) + manual QA + privacy review
**PENDING blockers:** none. **DECIDED (human, 2026-07-11): social proof ships anonymous-only at launch** — ADR-027 D6 resolves the buyer-data question with `anonymous` as the only enableable mode (no personal data published). The named display modes (`first_name`, `first_name_city`) exist in schema/admin behind an admin "pending privacy review" warning and **cannot be enabled for any store** until human/legal sign-off (11 Part C, same track as the ADR-025 beacon item, extended 2026-07-11) — that sign-off is external/non-blocking for this ticket.

### TICKET-036: Amazon FBA integration
**Phase:** 2
**Priority:** P2
**Area:** 6. Orders / 3. Inventory / 19. Shipping
**Depends on:** TICKET-003, TICKET-010, TICKET-032
**ADRs referenced:** ADR-001
**Spec files:** 02, 04, 06, 08
**Estimated complexity:** L
**Description:** FBA inventory sync, per-variant FBA availability (shipping exception per AC-172), "Buy on Amazon" affiliate button per locale, and **tracking auto-pull (mode 3 of DECIDED AF-C3)** — unconfirmed pulls mark fulfillment PENDING, never silently shipped.
**Acceptance criteria references:** AC-063, AC-172
**PENDING blockers:** none blocking beyond credentials/config screen details.

### TICKET-037: Currency converter admin screen
**Phase:** 2
**Priority:** P3
**Area:** 17. Currency conversion / 27. Super-admin settings
**Depends on:** TICKET-031
**ADRs referenced:** ADR-001, ADR-023
**Spec files:** 02, 04, 06
**Estimated complexity:** S
**Description:** Super-admin configuration of the 34 currencies (symbol, code, placement, enable), rate-source configuration and refresh cadence, per-store display-currency enablement. Per ADR-023: master `CurrencyDefinition`/`CurrencyRate` CRUD + the `CurrencyConverterSettings` singleton (auto-refresh toggle, rounding mode) live at `/superadmin/` (ADR-001 §3b tier, same as Theme/ShippingZone — corrects `06_settings_requirements.md` §8's "/admin/" placement for the master-data half); the per-store enable/disable checklist against that master list (`StoreCurrencySetting`) lives at `/admin/`. Admin dashboard surfaces per-currency staleness/last-refresh-error (ADR-023 §1) — deliberately not surfaced on the storefront (AC-160).
**Acceptance criteria references:** AC-160, AC-162
**PENDING blockers:** ~~rate source decision (shared with TICKET-031)~~ **RESOLVED (2026-07-10, ADR-023)**. No PENDING blockers remain on this ticket.

---

# Phase 3 — Polish & integrations

### TICKET-038: AI sales assistant chat
**Phase:** 3
**Priority:** P3
**Area:** 24. AI Job System / storefront
**Depends on:** TICKET-014, TICKET-029
**ADRs referenced:** ADR-024 (primary — full design), ADR-003 (cost accounting), ADR-008 (locale), ADR-012 (widget slot), ADR-018 (policy pages, antispam)
**Spec files:** 02, 03; `docs/adr/ADR-024-ai-sales-chat.md`; `docs/proposals/ai-chat-technology-proposal.md`
**Estimated complexity:** L
**Description:** Storefront sales-assistant chat, grounded in the live catalog + `StaticPage` policies via read-only tool use — **technology DECIDED-human 2026-07-10 (11:#10 resolved, ADR-024)**. Direct Claude API (streaming Messages, `claude-haiku-4-5` default, prompt caching), new `chat` app (`ChatSession`/`ChatMessage`/`StoreChatSettings`), short-lived SSE per reply on plain WSGI with `?stream=0` fallback, spend into `AiJobMetric`(`sales_chat`)+`StoreAiQuota`, antispam reuse + session/store caps + `CHAT_KILL_SWITCH`, GDPR per-store owner opt-in **OFF by default** (widget hidden until sub-processor terms accepted), 30-day retention purge, widget via ADR-012 chat slot (vanilla JS, no build step). Single ticket (backend + widget — widget is thin; Architect's call per coordinator). Excluded from v1: `chat_store_digest` AiJob (later enhancement, decision §8-6), any write/action tools (new ADR + Safety review required). **Mandatory Safety Agent gate before release** (public AI input surface + per-store spend).
**Acceptance criteria:** AC-CHAT-01…16 below (defined by ADR-024 "Tests required"; all Anthropic calls mocked in CI):
- **AC-CHAT-01 (opt-in gating):** widget absent and `/chat/*` endpoints refuse with explicit reason when `is_enabled=False`, when terms not accepted, or when `CHAT_KILL_SWITCH` on — each independently.
- **AC-CHAT-02 (acceptance audit):** enabling chat requires the sub-processor acceptance checkbox; `subprocessor_terms_accepted_at` + `accepted_by` stamped; disabling preserves the stamp.
- **AC-CHAT-03 (store scoping):** all four tools filter `.for_store(request.store)`; `get_product` with another store's id returns a not-found tool result; `chat` models pass the StoreOwnedModel compliance test.
- **AC-CHAT-04 (read-only invariant):** the tool layer performs no INSERT/UPDATE/DELETE (asserted in tests); tool set is exactly `search_products`/`get_product`/`list_collections`/`get_store_policy`.
- **AC-CHAT-05 (grounding rules):** versioned system-prompt fixture contains the tool-results-only pricing/shipping/discount rules, no-invented-coupons rule, permalink rule, PII-redirect rule; widget shows the permanent AI disclaimer.
- **AC-CHAT-06 (transport):** SSE reply streams incrementally and terminates with `event: done`; `?stream=0` returns the identical final text as JSON.
- **AC-CHAT-07 (rate limits):** message 21/hour per (store, IP) rejected; session-mint limit 5/hour per (store, IP).
- **AC-CHAT-08 (store daily budget):** store-level daily message budget rejects store-wide even from fresh IPs.
- **AC-CHAT-09 (session caps):** message 31 rejected with `ended_reason='message_cap'`; session token budget (150K) → `token_cap`.
- **AC-CHAT-10 (input hygiene):** >2000-char and empty messages rejected; history rebuilt server-side from last 12 `ChatMessage` rows — client-supplied history ignored.
- **AC-CHAT-11 (quota):** enforced `StoreAiQuota` exhausted ⇒ refusal BEFORE any Anthropic call (mock asserts zero calls) and widget shows the explicit unavailable state; usage recorded into `AiJobMetric` (`job_type='sales_chat'`) and `StoreAiQuota` after each call; **no `AiJob`/`AiJobRun` rows created for chat turns**.
- **AC-CHAT-12 (error path):** mocked Anthropic 429/500/refusal ⇒ user-visible error event, logged, session still usable — no invisible failure (§XV-1).
- **AC-CHAT-13 (locale):** `/fr` storefront session answers in French and tool results use published French translations with source-language fallback.
- **AC-CHAT-14 (retention):** purge task deletes sessions older than `CHAT_RETENTION_DAYS` (default 30), keeps newer.
- **AC-CHAT-15 (tool-loop cap):** mocked endless tool requests stop at 5 round-trips per turn with a final answer still produced.
- **AC-CHAT-16 (prompt caching):** stable prefix (system + digest slot + tools) ≥ 4096 tokens (Haiku cacheable minimum) with `cache_control` on the last system block; model id resolves `StoreChatSettings.model_id_override` → `CHAT_MODEL_ID`.
**PENDING blockers:** none — 11:#10 DECIDED-human 2026-07-10 (ADR-024). Release checklist additions: gunicorn threaded-worker sizing for SSE; verify Anthropic DPA/sub-processor listing before first store opt-in.

### TICKET-039: Multi-step upsell funnel builder (visual)
**Phase:** 3
**Priority:** P3
**Area:** 13. Up-sell & conversion
**Depends on:** TICKET-027, TICKET-028
**ADRs referenced:** ADR-007
**Spec files:** 02, 04, 08
**Estimated complexity:** L
**Description:** Visual funnel-step editor over the existing `CampaignStep` graph (stable-FK accept/decline branches): add/remove/reorder steps without positional renumbering, per-step offer config, branch visualization, split-test slots. Pure admin-UI layer — the state machine and payments are already live from Phase 2.
**Acceptance criteria references:** AC-141, AC-142
**PENDING blockers:** AF-C8 (order-bump split-test reporting location, MEDIUM).

### TICKET-040: Per-store custom email sender domain
**Phase:** 3
**Priority:** P3
**Area:** 12. Email marketing
**Depends on:** TICKET-022
**ADRs referenced:** ADR-001, ADR-034 (primary — full design, ACCEPTED human 2026-07-11)
**Spec files:** 02, 06; `docs/adr/ADR-034-sender-domains.md`
**Estimated complexity:** M
**Description:** DNS verification flow (SPF/DKIM records per store domain, verification polling with explicit status states — never inferred), fallback to the shared platform sender until verified, per-store sender switch. Extends DECIDED Settings-C1's v1. **Designed by ADR-034**: `SenderDomain` as a plain `Model` (not `StoreOwnedModel`, mirrors `StoreDomain`/ADR-008 — globally-unique `domain`), platform-generated DKIM keypair (private key via existing `EncryptedCharField`), `UNVERIFIED/PENDING/VERIFIED/FAILED` state machine with weekly re-check demotion, single `resolve_from_email(store)` resolver replacing both `emails/service.py` call sites, v1 scope honestly limited to verified custom From + SPF pass + platform-DKIM alignment (full custom-domain DKIM signing is an ops/relay runbook item, not code). Mandatory Safety Agent gate (domain verification = spoofing surface).
**Acceptance criteria references:** AC-183 (regression: rendering unchanged across senders)
**PENDING blockers:** none blocking implementation — `dnspython` dependency **DECIDED/APPROVED (human, 2026-07-11, ADR-034:P1)**: the automated DNS-check beat task ships against B1; the manual super-admin-verification fallback (B3) is no longer needed. Non-blocking follow-up only: re-verification cadence for `VERIFIED` domains (ADR-034:P2, recommended weekly) still awaits human confirmation of exact cadence.

### TICKET-041: Additional payment processors (HiPay, Mollie, BTCPay, BitPay)
**Phase:** 3
**Priority:** P3
**Area:** 22. Payment processing
**Depends on:** TICKET-018, TICKET-020
**ADRs referenced:** ADR-006, ADR-007
**Spec files:** 02, 06
**Estimated complexity:** L
**Description:** Four new connector-registry entries implementing the TICKET-018 interface (authorize, capture, refund, health-probe, `supports_delayed_capture` flag — crypto processors likely `false`, excluding their orders from post-purchase funnels automatically). No routing-engine changes required by design.
**Acceptance criteria references:** AC-132, AC-133, AC-134
**PENDING blockers:** vault backend decision (carried from TICKET-017) must be resolved before storing more credential families.

### TICKET-042: A/B multi-variant product pages
**Phase:** 3
**Priority:** P3
**Area:** 2. Product Catalog / 9. SEO / 8. Analytics
**Depends on:** TICKET-003, TICKET-012, TICKET-016
**ADRs referenced:** ADR-028 (primary — full design), ADR-005, ADR-004
**Spec files:** 02, 03, 07, 08; `docs/adr/ADR-028-ab-variant-pages.md`
**Estimated complexity:** M
**Description:** Same product served at multiple image-variant URLs (Pinterest surface + A/B testing) — **designed by ADR-028**: `ProductPageVersion` entity (image-set-only, zero translatable fields; "variant" naming banned to avoid ProductVariant/SKU collision), version URLs as ordinary Permalink rows (`<product-slug>-<suffix>`, own content type, all product languages) through the existing resolver + redirect machinery, **rel=canonical to primary enforced, own og:url/og:image, no hreflang, excluded from sitemap** (DECIDED FM-C4 / URL-004 / CAN-020 / META-004 / SM-030), link-level traffic split only (no assignment logic), measurement via `properties.page_version_id` on existing beacon events (ADR-004 vocabulary unchanged) + soft `page_version_id` stamps on CartItem/OrderItem, per-version results report with aggregation surviving the event purge. Two PRs: **042-A** (entity/URLs/rendering/SEO/admin) then **042-B** (measurement/report + activation of `ABVariantSlugNeverInLinkTest`'s negative case). Mandatory SEO Agent gate (042-A) and Safety Agent gate (beacon/form input, admin CRUD).
**Acceptance criteria references:** AC-017, AC-090, AC-093 (+ ADR-028 "Tests required" 1–10)
**PENDING blockers:** none — ADR-028 P1–P5 (product-level defaults: version cap 10, all-languages permalinks, ≥1 image to activate, no significance stats in v1, 302-deactivate/301-delete) all **DECIDED (human, 2026-07-11)**; ADR-028 status ACCEPTED. Implementation may proceed.

### TICKET-043: French bookkeeping order export API
**Phase:** 3
**Priority:** P3
**Area:** 6. Orders & Fulfillment
**Depends on:** TICKET-009, TICKET-010
**ADRs referenced:** ADR-001
**Spec files:** 02, 04
**Estimated complexity:** M
**Description:** Order + refund export API with VAT breakdown (the ONLY place VAT appears — engine is tax-free per DECIDED 11:#6) and **per-item ship-from country**, changeable at shipping validation. Token-authenticated, org-scoped.
**Acceptance criteria references:** AC-203
**PENDING blockers:** **11:#12 (CRITICAL): complete VAT rate/regime value spec for orders and refunds — Spec Agent draft must be validated before implementation.**

### TICKET-044: TimescaleDB analytics migration
**Phase:** 3
**Priority:** P2
**Area:** 8. Analytics & Reports
**Depends on:** TICKET-012, TICKET-013
**ADRs referenced:** ADR-004 (revised), ADR-035 (primary — full design, ACCEPTED human 2026-07-11)
**Spec files:** 02, 08; `docs/adr/ADR-035-timescaledb-migration.md`
**Estimated complexity:** M
**Description:** **Designed by ADR-035 — recommendation: defer the hypertable conversion, ship the guards now.** At zero production traffic there is nothing to prove a chunk-management policy against; ADR-004 already made the schema identical on both backends for exactly this reason. Scope for this ticket: a vendor/extension-aware `RunPython` migration gating `create_hypertable(if_not_exists=>TRUE, migrate_data=>TRUE)` (logged, never silently skipped — §XV-1), the matching `purge_events` branch (`drop_chunks` when a hypertable is detected, unchanged DELETE otherwise — both paths log which one ran), no change to `aggregate_metrics`/`AggregatedMetric` (already storage-agnostic), continuous aggregates explicitly out of scope (documented future path only). Extension install stays an ops/release-checklist item, never an automatic `migrate` side effect.
**Acceptance criteria references:** AC-080, AC-081, AC-082 (regression suite must pass unchanged on both backends)
**PENDING blockers:** none — ADR-035 ACCEPTED (human, 2026-07-11), already decisive at proposal time. Implementation may proceed (skip if TICKET-012 deployed TimescaleDB from day 1).

### TICKET-045: Automated gift card campaigns
**Phase:** 3
**Priority:** P3
**Area:** 5. Pricing, Coupons & Gift Cards / 13. Campaigns
**Depends on:** TICKET-007, TICKET-022, TICKET-027
**ADRs referenced:** ADR-002, ADR-007, ADR-029 (primary — full design)
**Spec files:** 02, 04, 08; `docs/adr/ADR-029-gift-card-campaigns.md`
**Estimated complexity:** M
**Description:** Campaigns auto-issuing `giftcard_auto` codes (idempotent via `CampaignReward` — the 765×$5 incident guard, resolved by ADR-029 as the legacy platform's own equivalent feature), **fixed-value only in v1** (designed by ADR-029 D3; `percent_of_order`/`percent_discount_coupon` reserved), frequency cap + zero-processor-charge self-gifting brake (D9), email-only delivery through TICKET-022 (D7), draft/publish/archived states, `provenance=campaign` audit trail, 5-year FR expiry floor with configurable validity per store markets (D6, DECIDED human 2026-07-11), refund voiding = deactivate unspent / flag spent for review (D9, DECIDED human 2026-07-11). Mandatory Safety Agent gate (money-adjacent).
**Acceptance criteria references:** AC-041, AC-042, AC-043 (+ ADR-029 "Tests required")
**PENDING blockers:** none blocking — ADR-029 ACCEPTED (human, 2026-07-11). Non-blocking follow-ups only: value modes beyond fixed $ (`percent_of_order`/`percent_discount_coupon` — reserved enum values, no schema/UI in v1), `GiftCardCampaignTranslation` + AiJob wiring, conditions rule builder.

### TICKET-046: Security badge designer
**Phase:** 3
**Priority:** P3
**Area:** 14. Lead capture & notifications / storefront
**Depends on:** TICKET-029
**ADRs referenced:** ADR-001, ADR-012, ADR-030 (primary — full design)
**Spec files:** 02, 06; `docs/adr/ADR-030-security-badges.md`
**Estimated complexity:** S
**Description:** Checkout/product-page trust-badge configurator: preset combinations, custom image upload, per-segment heading color styling, per-location enable (product page, cart, checkout). **Designed by ADR-030**: truthful-by-construction built-in preset registry (no third-party certification marks — deliberate, human-approved deviation from the CommerceHQ reference screenshots), self-verifying lock/SSL icons gated on `request.is_secure()`, payment-network logos derived from enabled `PaymentMethod.method_family` rows, custom-image upload (raster only) for genuine store-held certifications, rendered via `SecurityBadgeSlotProvider` into the existing ADR-012 `slot.security_badge` slot.
**Acceptance criteria references:** — (visual QA) (+ ADR-030 "Tests required")
**PENDING blockers:** none — ADR-030 ACCEPTED (human, 2026-07-11). Live re-render preview pane (D5) stays out-of-scope, non-blocking follow-up.

### TICKET-047: Employee permission matrix — admin UX (grid editor + invite flows)
**Phase:** 3
**Priority:** P2
**Area:** 1. Authentication & Permissions / 27. Super-admin settings
**Depends on:** TICKET-002, **TICKET-054** (enforcement core — vocabulary, resolver, structural mixin, guardrails)
**ADRs referenced:** ADR-033 (primary — full design, ACCEPTED human 2026-07-11), ADR-001 §3
**Spec files:** 02, 04 (AF-002, AF-108, AF-112), 06, 08; `docs/adr/ADR-033-permission-matrix.md`
**Estimated complexity:** M
**Description:** Implement ADR-033 D4 (UX layer) on top of TICKET-054's enforcement core:
custom `StoreEmployee` admin form — Full/Limited access segmented mode bound to
`full_access`, module grid rendered from `stores/modules.py::MODULES` as dynamic
`perm__<key>` ChoiceFields composed into `permissions_json` in `clean()` (Orders shows
Full/Limited/No, all others Full/No, `employees` 19th row per ADR-033 D1c default);
vanilla-JS grid hide on Full Access (toggle never wipes the stored grid); employee list
per AF-002 step 2 (name, email, active status dot, `last_login_at`); invite flow — Full
name/Email/Phone fields replacing the raw user FK (existing-email → link User; else
create `User(is_store_admin=True)` with unusable password), "Store invitation" email via
`emails.service.send_transactional_email` (AF-108) with `PasswordResetTokenGenerator`
set-password link, stamp-once `accepted_at` on completion (chat GDPR-stamp precedent);
`invited_name`/`invited_phone` additive nullable fields on `StoreEmployee` (never on
`User` — ADR-033 D4b isolation argument); deactivate toggle independent of hard delete
(AF-002 edge case); super-admin cross-store management per AF-112 with the same grid form
+ exposed store FK, per-store matrix per assignment (AF-C1 default). Tests: ADR-033
groups 6–7 (invite flow incl. cross-store isolation; grid round-trip). Designer pass
after; **Safety Agent gate MANDATORY before release** (permission system: escalation,
lockout, invite tokens, IDOR on employee rows) — covers TICKET-054 + this ticket jointly.
**Acceptance criteria references:** AC-001, AC-002, AC-005, AC-006, AC-201
**PENDING blockers:** none — all ADR-033 D7 items DECIDED (human, 2026-07-11): AF-C1 (per-store matrix); Orders "Limited access" semantics (view-only); `employees` 19th grid row vs folded into Settings (separate row); phone informational-only (2FA out of scope). Implementation may proceed without restriction.

### TICKET-048: Consent core — `consent` app, cookie/state, gating, endpoint
**Phase:** 3
**Priority:** P2
**Area:** 21. Pixels & Analytics integrations / GDPR-ePrivacy
**Depends on:** TICKET-012 (FiredPixel/analytics beacon), TICKET-030 (pixels app)
**ADRs referenced:** ADR-025 (primary — full design), ADR-022 (D0 correction, gating touch points), ADR-004 (beacon), ADR-012 D9 (slot registry, reused not extended)
**Spec files:** 02, 06 §9; `docs/adr/ADR-025-consent-management.md`
**Estimated complexity:** L
**Description:** New `consent` Django app resolving the cross-backlog GDPR blocker (TH-141,
ADR-022 D7, KNOWN_RISKS item 1): `models.py` (`ConsentSettings`, `ConsentRecord` —
schema spec 05 §7b), `policy.py` (`CONSENT_POLICY_VERSION`, category constants),
`state.py::get_consent(request) -> ConsentState` (the single resolution function per
§XV-4 — frozen dataclass `decided/analytics/marketing/am_objected` +
`allows(category)`), `views.py` (`POST /_consent/`: validate → INSERT `ConsentRecord`
→ `Set-Cookie` on 204, insert-before-cookie so a failed proof write never sets a
cookie), `management/commands/purge_consent_records.py` (13-month rolling purge, DECIDED
P4). Gating touch points outside the app, exhaustively per ADR-025 D7: `pixels/registry.py`
(`PixelProvider.consent_category` attribute + values), `pixels/slot_provider.py`
(category filter via `get_consent`), `pixels/service.py::claim_purchase_pixels` gains
required `allowed_categories: frozenset[str]` param (the D0 correction — claims, not
only render, are consent-gated), `pixels/templates/pixels/ga_base.html` (Consent Mode
v2 default line), `storefront/views_checkout.py::order_thank_you` (passes allowed
categories into `claim_purchase_pixels`), `storefront/templatetags/storefront_tags.py`
(analytics beacon tag no-ops on `am_objected` / `beacon_requires_consent` without
`analytics` grant). Launch-checklist wiring: active `Pixel` row + consent disabled +
no `non_eu_acknowledged` ⇒ blocking error (ADR-025 D6). Fail-closed shim: with the
`consent` app absent from `INSTALLED_APPS`, `get_consent` returns an
undecided/all-refused-equivalent state (pixels dark, never unconsented firing).
**049 (banner UI) depends on this ticket; both ship in the same release — 048 alone
gates pixels with no way to ever grant, which is safe but pointless (ADR-025 D7).**
**Acceptance criteria sketch (from ADR-025 "Tests required" §1–9, §11, §13–14):**
- Undecided/malformed/stale-policy-version cookie ⇒ zero consent-gated pixel snippets
  render; granted-analytics ⇒ GA4 only; granted-marketing-only ⇒ inverse;
  refused-all ⇒ nothing renders.
- `POST /_consent/` sets `pradize_consent` (HttpOnly, SameSite=Lax, Max-Age=180d,
  Secure in production); 204 on success; 403 without CSRF token; rate-limited via
  existing antispam helpers; simulated proof-insert failure ⇒ 500 and **no**
  `Set-Cookie` header.
- Each action (accept_all/refuse_all/custom/withdraw) writes one `ConsentRecord`
  matching `consent_id`/categories/`policy_version`/lang/source; store-scoped (tenant
  isolation test: store A's records invisible to store B); purge command deletes only
  rows older than 13 months.
- **D0 regression (critical):** first PAID thank-you GET with no consent ⇒
  `claim_purchase_pixels` creates zero `FiredPixel` rows and renders nothing;
  subsequent GET with marketing granted ⇒ rows created once, snippets render once;
  further reload ⇒ nothing (AC-110 intact). `allowed_categories=frozenset()` ⇒ empty
  set regardless of PAID gate.
- `initiate_checkout`: first checkout GET without consent ⇒ exempt analytics event
  still fires, claim consumed, no pixel snippet; later-consented reload ⇒ still no
  InitiateCheckout snippet (documented, intended under-fire).
- Beacon: undecided ⇒ inline script present (exemption default); `am=0` ⇒ beacon
  no-ops; `beacon_requires_consent=True` store ⇒ beacon absent until analytics
  granted; schema-pinned test asserts the `Event` model has no IP field.
- GA4 Consent Mode v2: analytics-only grant emits `ad_storage`/`ad_user_data`/
  `ad_personalization` denied + `analytics_storage` granted; all-granted emits all
  granted; the four other providers get no consent-mode parameters.
- Launch checklist: active Pixel + consent disabled + no acknowledgment ⇒ blocking
  error; acknowledgment set ⇒ no error; enabled without `policy_page` ⇒ warning only.
- Fail-closed shim test: `consent` app not installed ⇒ `get_consent` fallback yields
  undecided ⇒ pixels dark.
- Security: malformed/oversized cookie values rejected to undecided without
  exception; `/_consent/` rejects non-boolean payloads; no user-supplied string from
  the endpoint is ever rendered into HTML.
**Named gates:** **Safety Agent gate required** — public unauthenticated POST endpoint
(`/_consent/`), cookie parsing/validation, rate limiting, and the purchase-claim
consent-gating logic are exactly the "auth, public input, background jobs" trigger
class (pipeline step 11). Safety review must confirm the fail-closed shim, the
insert-before-cookie ordering, and that no injected string reaches rendered HTML.
**PENDING blockers:** legal sign-off on the beacon-exemption reading
(`11_uncertainties_to_validate.md`, owner human/legal) does not block this ticket —
the fallback (`beacon_requires_consent=True` platform default) requires no redesign.

### TICKET-049: Consent banner UI — `slot.consent` provider, panel, footer control
**Phase:** 3
**Priority:** P2
**Area:** 21. Pixels & Analytics integrations / storefront / GDPR-ePrivacy
**Depends on:** TICKET-048 (consent core — cookie/state/endpoint must exist first), TICKET-029 (theme system, slot registry, theme tokens)
**ADRs referenced:** ADR-025 (primary — D5, D6 i18n/admin), ADR-012 D9 (existing `slot.consent`, `PREVIEW_SUPPRESSED_SLOTS`, TH-045 conformance)
**Spec files:** 02, 06 §9, 07 (SEO/CLS); `docs/adr/ADR-025-consent-management.md`
**Estimated complexity:** M
**Description:** `ConsentSlotProvider(SlotProvider)` registering into the already-declared
`slot.consent` (ADR-012 D9 — present in `base.html`/`base_checkout.html`, no new slot):
renders (a) the fixed-bottom-bar banner only when state is undecided, three
equal-visual-weight controls (`Accept all` / `Refuse all` / `Customize`, DECIDED P1
copy from the `.po` catalog), (b) the hidden preferences panel markup (always
present, per-category toggles — `necessary` locked ON, `analytics`/`marketing`
default OFF, audience-measurement exempt row per D4), (c) one inline `<style>` + one
inline vanilla-JS block (T029-B: no build step). Banner POSTs to `/_consent/`
(TICKET-048); JS reloads (`location.reload()`) on any newly-granted category, closes
silently otherwise. `footer.html` gains a `<button data-consent-manage>` ("Cookie
preferences") reopening the panel when the feature is enabled — suppressed on
`base_checkout.html` (documented gap, ADR-025 D5, footer already suppressed there
for CK-006 distraction-free checkout). Accessibility: `role="dialog"`,
`aria-modal="false"`, `aria-live="polite"`, focus-visible, Escape closes. Admin:
"Cookie consent" settings card (enable toggle + acknowledgment flow, policy-page
picker, read-only category→provider mapping, link to filtered `ConsentRecord`
changelist). i18n: all strings wrapped `{% trans %}`/`gettext`, shipped in `locale/fr/`
(French day-1, DECIDED P1); other languages fall back to English (multilingual debt
list). Theme conformance: skinned exclusively via ADR-012 theme tokens — zero
per-theme markup, all 3 themes conform by construction (TH-045 regression).
**Acceptance criteria sketch (from ADR-025 "Tests required" §10, §12):**
- `slot.consent` renders the banner for undecided state on every page type including
  checkout (`base_checkout` inherits it) and thank-you; theme preview renders no
  banner (`PREVIEW_SUPPRESSED_SLOTS` regression, TH-007-equivalent for consent);
  footer manage button present when enabled, absent when disabled; TH-045 conformance
  suite passes with the provider registered.
- i18n: banner renders French strings on a `fr` storefront request; English fallback
  otherwise.
- CLS/LCP: banner is server-rendered in the initial HTML (no late JS injection),
  `position:fixed` (overlays, does not displace layout — zero CLS by construction),
  compact (max-height capped ~40vh mobile, panel scrolls internally), inline `<style>`
  in the same response (no extra render-blocking request); crawler-visible banner
  markup is not cloaking (bots get identical HTML, never consent, pixels never render
  for them).
- Equal-weight buttons: no dark-pattern styling difference between Accept/Refuse
  (visual regression / DOM-attribute assertion, not just a screenshot).
**Named gates:**
- **Safety Agent gate required** — banner JS reads/sends CSRF token, panel POST path
  shares the TICKET-048 endpoint surface; review the client-side code for XSS
  surface (no user-supplied string interpolated into the inline `<script>`/`<style>`).
- **SEO Agent gate required** — confirm zero CLS contribution and that the banner
  does not trigger Google's intrusive-interstitial flag (fixed bottom bar, never a
  full-screen modal/scroll-wall per ADR-025 D5); confirm banner boilerplate text does
  not read as cloaking; confirm hreflang/canonical output is unaffected by the new
  slot content.
**PENDING blockers:** none — P1 copy DECIDED (human, 2026-07-11); depends on
TICKET-048 shipping first, both required in the same release (ADR-025 D7).

### TICKET-050: Isolation-layer fix — relation-bound M2M reads + cross-store M2M write guard
**Phase:** hotfix (blocks two live admin change views)
**Priority:** P0
**Area:** core isolation layer (ADR-001 §4)
**Depends on:** none (ADR-031 ACCEPTED 2026-07-11)
**ADRs referenced:** ADR-031 (primary — implementation sketch included), ADR-001 §4 (amended), ADR-029 addendum 2026-07-11, BUG_TESTS.csv `DISCOUNTS-INLINE-ISOLATION` note
**Spec files:** `docs/adr/ADR-031-relation-bound-m2m-reads.md`
**Estimated complexity:** M
**Description:** Implement ADR-031 exactly as sketched:
1. `core/managers.py` — `StoreScopedManager.get_queryset()` returns a real queryset
   when `self` has BOTH `.instance` and `.through` (M2M relation-bound manager);
   `_RaisingQuerySet` otherwise (direct access and reverse-FK managers unchanged).
2. New `core/m2m_guard.py` — `m2m_changed` pre_add guard raising `IsolationError`
   on any cross-store M2M row; auto-connected in `CoreConfig.ready()` for every
   M2M whose two ends are both StoreOwnedModel (five today:
   DiscountCode.product_conditions/collection_conditions,
   GiftCardCampaign.trigger_products, LeadCaptureCampaign.excluded_pages,
   FeedConfig.collections).
3. Remove `GiftCardCampaignAdmin.save_related` through-table bypass; scope its
   trigger_products widget to the campaign's store on change views.
4. Fix `DiscountCodeAdmin.get_form`: `Product.objects.filter(store=request.store)`
   chains off the raising queryset — replace with `.for_store(request.store)`.
5. Tests per ADR-031 §Tests required (5 groups: full-HTTP rendering of the four
   affected change views, stock save_m2m round-trip, cross-store write rejection,
   loud-isolation status-quo pins, guard/compliance introspection). After-Bug Test
   Agent proof cycle mandatory (revert manager change → IsolationError).
**PENDING blockers:** none.

### TICKET-051: Isolation-layer closure — formset PK lookups + non-raising terminal methods
**Phase:** hotfix (blocks every StoreOwnedModel inline POST, incl. product variants; silent cross-store reads/writes possible)
**Priority:** P0
**Area:** core isolation layer (ADR-001 §4)
**Depends on:** TICKET-050 (shipped, PROVEN)
**ADRs referenced:** ADR-031 addendum 2026-07-11 (primary — implementation sketch included), ADR-001 §4 (re-amended), BUG_TESTS.csv `CAMPAIGN-STEP-INLINE-ISOLATION` / `RAW-ID-WIDGET-ISOLATION` notes
**Spec files:** `docs/adr/ADR-031-relation-bound-m2m-reads.md` (addendum section)
**Estimated complexity:** L (mechanism is S; the terminal-method audit/migration is the bulk)
**Description:** Implement the ADR-031 addendum exactly as sketched:
1. New `core/formsets.py` — `StoreSafePKFormSetMixin` + `StoreSafeInlineFormSet` +
   `StoreSafeModelFormSet` (hidden-pk queryset swap to the formset's own scoped
   queryset; only when the stock queryset is a `_RaisingQuerySet` over `self.model`).
2. New shared inline base (`core/admin.py` `StoreOwnedInlineMixin`: formset preset +
   the `get_queryset() → cross_store_unsafe()` convention); migrate all 21 inlines;
   re-parent `campaigns._FormsetWithDefaultPosition` onto `StoreSafeInlineFormSet`;
   `StoreCurrencySettingAdmin.get_changelist_formset` injects `StoreSafeModelFormSet`.
3. `core/managers.py` — `_RaisingQuerySet` raises on `aggregate/count/exists/contains/
   iterator/aiterator/update/bulk_update/delete/explain`; add `StoreScopedManager.none()`
   returning a real empty queryset.
4. Migration audit (mandatory, same commit series): every `<StoreOwnedModel>.objects.filter(...)`
   chain ending in a newly-raising terminal — known: `discounts/service.py:194,690,703`
   atomic updates → `.for_store(code.store).filter(pk=...)`; each other hit is converted
   to `.for_store()`/`.cross_store_unsafe()` or escalated as a latent cross-store bug.
5. Tests per ADR-031 addendum §Tests required (6 groups: inline POST full-HTTP,
   changelist-editable POST, pk-tamper rejection, parametrized terminal-method matrix
   + status-quo pins + `none()`, atomic-decrement regression, 3 drift tests). After-Bug
   Test Agent proof cycle mandatory on groups 1-2 (revert mixin → IsolationError).
**PENDING blockers:** none.

### TICKET-052: Isolation validation ring — validation window + scoped choice widgets (TREE RED)
**Phase:** hotfix (suite red: 27/28 failures, one root cause — full_clean()/admin validation broken for every StoreOwnedModel with unique constraints)
**Priority:** P0 — implement immediately, blocks all other work
**Area:** core isolation layer (ADR-001 §4)
**Depends on:** TICKET-051 (shipped, PROVEN — this closes the ring it surfaced)
**ADRs referenced:** ADR-031 Addendum 3 2026-07-11 (primary — implementation sketch included), ADR-001 §4 (re-amended), BUG_TESTS.csv `TERMINAL-METHODS-ISOLATION`
**Spec files:** `docs/adr/ADR-031-relation-bound-m2m-reads.md` (Addendum 3)
**Estimated complexity:** M
**Description:** Implement ADR-031 Addendum 3 exactly as sketched:
1. D1 — `core/managers.py`: `_MODEL_VALIDATION_WINDOW` contextvar; `_RaisingQuerySet.exists()`
   permitted only inside the window (all other terminals keep raising, in-window included).
   `core/models.py` StoreOwnedModel: `validate_unique`/`validate_constraints` wrappers
   (token set/reset in try/finally). User `clean()` code stays outside the window.
2. D2 — `core/admin.py`: `StoreScopedFormFieldsMixin` (`formfield_for_foreignkey`/
   `formfield_for_manytomany` defaults for StoreOwnedModel targets: store site →
   `.for_store(request.store)`, super site → `.cross_store_unsafe()`, narrowed to
   `obj.store` where an instance is bound); mix into `StoreOwnedInlineMixin` and the
   admin base classes; retire now-redundant per-admin overrides where behavior-identical.
3. D3 — ordinary dev fixes, no architecture: `CsvListWidget`×`JSONField.bound_data`
   crash; pre-existing feeds `RegenerateActionTest` failure.
4. Tests per Addendum 3 §Tests required (6 groups: validation-ring green incl. full-HTTP
   unique-violation POSTs; window-narrowness probes; cross-store-allowed/in-store-rejected
   semantics pin; introspective widget-queryset drift test over both sites; D3 regressions;
   proof cycles for D1 and D2). After-Bug Test Agent proof cycle mandatory.
**PENDING blockers:** none.

---

### TICKET-053: Admin unique validation for store-excluded forms + CsvListWidget display/corruption fix
**Phase:** hardening (pre-existing, repo-wide: same-store duplicates through any store-site admin 500 with a raw IntegrityError instead of a form error)
**Priority:** P1 — admin-wide correctness gap; independent of TICKET-052's fix (which is proven complete at the model layer)
**Area:** core admin layer (registration-mixed form validation) + core widgets
**Depends on:** TICKET-052 (shipped, PROVEN — this makes store-excluded admin forms reach the validation ring TICKET-052 repaired)
**ADRs referenced:** ADR-032 (primary — implementation sketch included, Django 6.0.4 source-traced), ADR-031 Addendum 3 (the proven chain being reached), BUG_TESTS.csv `VALIDATION-WINDOW-ISOLATION` (the "separate finding for the Architect Agent")
**Spec files:** `docs/adr/ADR-032-admin-unique-validation.md`
**Estimated complexity:** M
**Description:** Implement ADR-032 exactly as sketched:
1. D1 — `core/admin.py` `StoreUniqueValidationFormMixin`: inject `instance.store` before
   `_post_clean()` AND discard `"store"` from `_get_validation_exclusions()` when set.
   Both primitives are load-bearing (exclusion tuple-skip + None-value skip are two
   independent kill switches — see ADR Context).
2. D2 — wiring by construction: `StoreScopedFormFieldsMixin.get_form`/`.get_formset`
   wrap the per-request form class (store site: `request.store`; super site / inlines:
   no injection); `StoreSafePKFormSetMixin._construct_form` stamps the parent's store
   onto inline instances. Zero per-admin edits; `save_model`/`save_formset` conventions
   untouched (they become idempotent re-asserts).
3. D3 — super-site structural rule: add-capable super-site admins over constrained
   StoreOwnedModels must expose `store` as a form field (DiscountCodeSuperAdmin
   pattern); enforced by the sweep test, grandfathered exceptions must be listed
   explicitly in the test.
4. D4 — deliberately NOT built: global IntegrityError catcher (rejected in ADR-032
   Options (c); residual TOCTOU race = stock-Django posture, documented).
5. D5 — CsvListWidget 3-part fix (ordinary dev item): `CsvListFormField.prepare_value`
   list passthrough (unblocks the `red, blue` rendering AND fixes the no-op-save
   corruption that writes `tags=['[]']`); `to_python` empty→`[]`; `blank=True` +
   migrations on `Product.tags` / `Organization.coverage_areas_json`; one-off check
   for already-corrupted `'[]'` rows.
6. Tests per ADR-032 §Tests required: structural-semantic sweep over both sites
   (decided over full-HTTP-per-admin — rationale in the ADR) + 2 HTTP canaries
   (StaticPage duplicate-slug ADD POST — the exact TICKET-052 failing reproduction —
   and one inline cross-form dedup canary) + cross-store-still-valid guard-rail +
   D5 regressions. After-Bug Test Agent proof cycle mandatory (both D1 primitives
   proven load-bearing independently).
**PENDING blockers:** none.

### TICKET-054: Permission enforcement core — module vocabulary, single resolver, structural gating, guardrails
**Phase:** 3
**Priority:** P2 (blocks TICKET-047)
**Area:** 1. Authentication & Permissions / core admin layer
**Depends on:** TICKET-002 (storage, shipped), TICKET-052 (registration-wrap precedent, shipped)
**ADRs referenced:** ADR-033 (primary — full design incl. the D3c module_key audit table, ACCEPTED human 2026-07-11), ADR-001 §3, ADR-031/032 (drift-test precedent)
**Spec files:** 04 (AF-002 grid = vocabulary source), 11 (18:A2, AF-C1, Part D); `docs/adr/ADR-033-permission-matrix.md`
**Estimated complexity:** M
**Description:** Implement ADR-033 D1/D2/D3/D5 exactly as designed:
1. D1 — `stores/modules.py` frozen `MODULES` registry (18 AF-002 modules + `employees`;
   shipped keys canonical: `pages`="CMS", `analytics`="Reports"); reversible data
   migration merging `collections` into `products` (max hierarchy rank) — the ONLY key
   migration; `StoreEmployee.clean()`/save-guard rejecting unknown keys and undeclared
   levels (§XV-5), preceded by a legacy-key-strip migration.
2. D3 — `stores/permissions.py`: `get_store_employee(request)` (request-cached, one query),
   `check_module_access(request, key, level)` (§XV-4 single resolver — supersedes the
   seven per-app `_check_module_access` copies in badges/catalog/orders/engagement/pages/
   feeds/stores admin.py, all deleted), `require_module(key, level)` decorator (migrate
   analytics/admin_views.py, feeds regenerate view, pages contact inbox, stores
   add-language view). `StoreModulePermissionMixin` (view=`limited`, mutate=`full`)
   auto-mixed by `StoreAdminSite.register()` (store site ONLY — super site never
   matrix-gated); `module_key` declarations per the ADR-033 D3c audit table (str /
   any-of tuple for campaigns / `MODULE_EXEMPT` for core's read-only profile admin);
   unmapped admins fail closed for limited employees; introspective drift test over
   `store_admin_site._registry` (unmapped or invalid key = failure; unclaimed
   non-reserved module = failure).
3. D5 — guardrails at the persistence boundary: own-row change/delete denied on the store
   site + self-escalation rejected in `clean()`; last-active-`full_access` demote/
   deactivate/delete blocked under `select_for_update` (§XIII), super site
   allows-with-warning; zero-full-access stores (open-default whitelist case) unaffected.
4. Tests: ADR-033 groups 1–5, 8–9 (resolver branches + cache query count; drift test
   proven to fail on a dummy unmapped registration; representative gating matrix per
   mapping row; vocabulary validation + merge migration; guardrails incl. After-Bug-style
   concurrency proof for the ≥1→0 race; admin-index single-employee-query count; all
   existing per-app permission tests green after helper migration).
Safety Agent gate is taken jointly with TICKET-047 before release.
**Acceptance criteria references:** AC-001, AC-002, AC-005, AC-006, AC-201
**PENDING blockers:** none — D3c mapping ASSUMPTIONS (campaigns tuple, cart→orders, chat→apps, consent/emails/shipping→settings, discounts→gift_cards) DECIDED/approved as-is (human, 2026-07-11); each remains a one-line `module_key` edit if a future need arises.

---

## Cross-backlog PENDING blockers needing human decision (deduplicated, by urgency)

| Blocker | Blocks | Needed by | Status |
|---|---|---|---|
| ~~`vault://` secret backend~~ | T017 finish, T041 | Phase 1 mid | **RESOLVED (2026-07-02)**: django-fernet-fields, FERNET_KEY env var (Settings-C3) |
| ~~Canonical order payment/fulfillment status enums~~ | T009 migration freeze | Phase 1 early | **RESOLVED (2026-07-02)**: Payment: PENDING/PAID/PARTIALLY_REFUNDED/REFUNDED/FAILED/CANCELLED; Fulfillment: NOT_SENT/SENT_TO_FULFILLMENT/PARTIALLY_SHIPPED/SHIPPED |
| ~~Inventory mode scope (AC-U2)~~ | T003/T004 | Phase 1 early | **RESOLVED (2026-07-02)**: per variant; `inventory_mode` moved to ProductVariant |
| ~~UF-K refund instrument order (AC-U3)~~ | T007/T045 | Phase 3 | **RESOLVED (2026-07-02)**: card refunded first, remainder to gift card |
| ~~FM-C2 credits/membership model~~ | T014 | Phase 1 | **RESOLVED (2026-07-02)**: StoreAiQuota — monthly token quota per store |
| ~~FiredPixel placement~~ | T012 | Phase 1 | **RESOLVED (2026-07-02)**: main transactional DB (not analytics DB), with order FK |
| ~~Store↔Organization grouping (AC-U2 arch)~~ | T002/T017 | Phase 1 | **RESOLVED (2026-07-02)**: nullable org FK on Store, payment-only, no config inheritance |
| ~~Safety Agent review of combined capture (11:#5)~~ | T028 start | Phase 2 | **RESOLVED (2026-07-03)**: all 9 amendments applied to ADR-007 and T028. Two-charge baseline, race guard, processor pinning, access control, provisional items, multi-charge refund model all incorporated. T028 cleared for implementation after T027. |
| ~~AF-C4 send-delay field placement~~ | T021 finish | Phase 2 | **RESOLVED (2026-07-04)**: `send_delay_hours` field inside the email wizard ("Send after" number + hours/days dropdown, required, default 1 h) |
| ~~Up-sell precedence / buy-X-get-X cap / storewide next-order constraints~~ | T027 finish | Phase 2 | **RESOLVED (2026-07-04)**: store-admin campaign wins (super-admin visible read-only); no free-value cap; single-use `CampaignCode` with configurable expiry, default 30 days |
| Designer theme proposals (11:#11) + FM-C1b storefront designs | T029 (3 themes) | Phase 2 | Open |
| Analytics column denominators / attribution window (Part A) | T013 finish | Phase 1 late | Open |
| Org-split × method-strategy layer composition (Part E) | T020 finish | Phase 1 late | Open |
| ~~Domain→language→country model (11:#9)~~ | T016/T024 URL prefixing | Phase 1 late | **RESOLVED (2026-07-03)**: ADR-008 — `StoreDomain`/`StoreLanguage`/`ShippingCountry`, `resolve_locale`, country on StoreLanguage. T016/T024/T025/T026/T033 unblocked. |
| AI chat technology (11:#10) | T038 | Phase 3 | Open |
| French bookkeeping VAT spec (11:#12) | T043 | Phase 3 | Open |
| Gift-card value modes / jurisdictional expiry | T045 (and T007 edge paths) | Phase 3 | Open |
| ~~GDPR consent gating for pixels (TH-141, ADR-022 D7, KNOWN_RISKS item 1)~~ | T030 EU-launch readiness | Phase 3 | **RESOLUTION IN PROGRESS (2026-07-11)**: design ACCEPTED — `docs/adr/ADR-025-consent-management.md` (human, 2026-07-11), all four P-items DECIDED. Implementation is T048 (consent core) + T049 (banner UI); ADR-022 D7's "slot-provider-only" wording corrected by dated addendum. Legal sign-off on the beacon-exemption reading remains open (owner human/legal, non-blocking). |
| GDPR: emailing non-purchasers, popup buyer data, PII erasure | T021, T035, T011 policies | Phase 2 | Open (unrelated to the consent-gating item above — ADR-025 Context explicitly scopes these out; still open) |
