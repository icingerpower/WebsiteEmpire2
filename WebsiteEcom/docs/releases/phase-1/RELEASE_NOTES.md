# Release Notes — Phase 1 (Technical Foundation)

> Status: PHASE 1 COMPLETE — all gates passed (see RELEASE_CHECKLIST.md).
> Release Manager final verdict: 2026-07-03. Phase 2 may begin.

## What Phase 1 delivers

Phase 1 is the technical foundation of the Pradize Django ecommerce engine. It is not
a sellable product — it is the complete data layer, admin interface, business logic,
and infrastructure that Phase 2 (first sellable storefront) builds on.

### Multi-tenant store isolation (ADR-001)

- `StoreOwnedModel` abstract base with `StoreScopedManager` that raises on unscoped
  queries; every store-scoped model uses it without exception.
- `_RaisingQuerySet` makes an unscoped access a loud failure, not silent data bleed.
- Model introspection test (`core/tests/test_store_owned_model_compliance.py`) fails
  CI if a new model is added without the base class.
- Two `AdminSite` instances: `/admin/` (per-store) and `/superadmin/` (cross-org).
- `StoreEmployee` with 18-module permission matrix storage.

### Product catalog with multilingual support

- `Product` + `ProductVariant` (per-variant inventory mode, 7 modes).
- `Collection` (smart rules + manual ordering with stable ordering keys).
- Shared NFD-normalize slug function; `pre_save` signal for auto-redirect creation.
- `ProductImage`, video embed URL (YouTube/Vimeo only — AF-C2 decision).

### Cart and checkout (ADR-007)

- `Cart` + `CartItem` with 30-day session persistence.
- Checkout service: address capture, discount application, payment authorization,
  order creation with UF-I idempotency key.
- Capture-window flag set for post-purchase upsell eligibility (Phase 2 ticket T028).

### Coupon and gift card stacking (ADR-002)

- Unified `DiscountCode` model (6 type variants), append-only `GiftCardTransaction`
  ledger, `DiscountCodeEmailUse` per-email counter.
- Atomic `UPDATE … WHERE uses_remaining > 0` pattern; `SELECT FOR UPDATE` for gift
  cards; `ON CONFLICT … DO UPDATE … WHERE use_count < per_email_limit` for per-email.
- `CampaignReward` idempotent issue guard (765×$5 incident pattern).

### Payment routing engine (ADR-006)

- `OrganizationRule` two-stage decision tree (geography → allocation).
- `ProcessorSplitCounter` running-counter percent-split (never per-order random).
- Health-check at evaluation time with automatic fallback chain.
- `DecisionLog` written on every routing decision (no sampling).
- Stripe + PayPal connectors (day-1 processors per 11:#7 decision).
- `EncryptedCharField` for processor credentials (Fernet, `cryptography` library).
- Multi-account webhook dispatch (tries each active account's webhook_secret).

### Stripe and PayPal webhook dispatch

- Idempotent guards: transition payment_status only if PENDING.
- Coupon/stock restore on released authorizations in the same webhook transaction.
- PayPal delayed-capture confirmed (Orders API v2 AUTHORIZE intent).

### Analytics pipeline (ADR-004)

- Separate analytics DB (`analytics` Django DB alias); `Event` table with soft IDs.
- Async Celery ingest (`ingest_beacon_event`); `MAX_EVENTS_PER_BEACON = 50` cap.
- `BeaconDlq` dead-letter queue for failed batches; dropped-batch counter.
- `FiredPixel` dedup table in main DB (insert-before-fire pattern).
- First-touch UTM middleware; server-side purchase event at payment confirmation.
- Nightly `aggregate_metrics` job + `check_purchase_mismatch` detective.
- 13 report types reading analytics DB only.

### AI job system (ADR-003)

- `AiJob`, `AiJobRun`, `AiJobDependency`, `AiJobOutput`, `AiJobValidation`,
  `AiJobMetric`; explicit `NOT_STARTED/IN_PROGRESS/DONE/ERROR/SKIPPED` status.
- Plugin registry pattern for job types (`validate_output`, `build_prompt`,
  `persist_output`).
- Heartbeat reclaim: dead > 3× interval → `ERROR(dead)` + re-queue.
- Output validation gate: length ratio, stop_reason assertion, all-fields-or-nothing.
- `StoreAiQuota` monthly token quota per store.

### Permalink and slug redirect management (ADR-005)

- `PermalinkTranslation` (URL-owning rows, unique per store+lang+slug).
- `PermalinkResolver` as the single internal-URL builder.
- `PermalinkRedirect` with chain collapse at write time; loop rejection; `none` type.
- `pre_save` auto-301 creation on slug change; >7-days-published confirmation gate.

### Store and super-admin interfaces

- Per-store and super-admin Django admin screens for all models.
- Store-switcher middleware; host-resolution middleware stub.

## Ticket completion

Phase 1 tickets T001–T020 are all implemented. T027–T028 (upsell funnel, Phase 2)
are NOT implemented — they are cleared for implementation after Phase 1 closes.
