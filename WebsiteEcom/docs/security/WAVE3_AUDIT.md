# Wave-3 Pre-Release Security Audit

**Scope:** Page versions (TICKET-042 / ADR-028), gift-card campaigns
(TICKET-045 / ADR-029 — mandatory money-adjacent gate), security badges
(TICKET-046 / ADR-030).
**Auditor:** Safety Agent · **Date:** 2026-07-11 · **Posture:** defensive
review only; no production code was modified.
**Test evidence:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3
manage.py test discounts payments badges catalog analytics cart` —
**993 tests, all OK** (SQLite; see W3-2 for why SQLite green is not
sufficient evidence for the analytics finding).

**Known tracked issue (not re-discovered here):** the M2M-to-StoreOwnedModel
**read-path** `IsolationError` (`model_to_dict` → `_RaisingQuerySet`) blocks
the `DiscountCode` and `GiftCardCampaign` admin change views (documented in
the ADR-029 addendum). An Architect is designing the fix in parallel. It is
an availability/usability blocker for the admin, not a data-exposure issue,
and is excluded from this audit's verdict except as a release-manager
dependency.

---

## Findings index

| ID | Area | Severity | Title |
|---|---|---|---|
| W3-1 | Gift cards / payments | **MEDIUM (fix before release)** | REFUNDED→PAID transition not refused by either processor's PAID handler; REFUNDED reachable from PENDING/FAILED |
| W3-2 | Page versions / analytics | **MEDIUM (fix before release)** | Beacon `properties.page_version_id` not coerced at ingest → Postgres `CAST` failure kills the daily aggregation for **all stores** (the exact ADR-028 safety-gate item) |
| W3-3 | Page versions / analytics | MEDIUM | Unbounded `AggregatedMetric` cardinality from unvalidated public `product_id` / `page_version_id` |
| W3-4 | Gift cards | MEDIUM (accepted-risk, needs the ADR-mandated mitigation) | Default caps off: farmable liability; D5's "recommend the cap" admin help text is missing |
| W3-5 | Gift cards / emails | LOW (re-review trigger attached) | Admin-authored subject/body rendered as Django templates with model instances in context |
| W3-6 | Gift cards | LOW | Idempotency axis includes `recipient_email`: replay after an order-email change can double-issue |
| W3-7 | Gift cards | LOW | FR 5-year floor: publish-time only; no issuance-time re-check; inert on programmatic `.save()` (F1 class) |
| W3-8 | Gift cards / admin | LOW | `trigger_products` cross-store selection possible in super-admin; silently never matches (§XV-1) |
| W3-9 | Badges | LOW | `border_color` lacks the hex validation `heading_segments_json` colors get |
| W3-10 | Badges / deployment | LOW | Media serving hardening for `custom_image` (nosniff; extension allow-list on the model field) |
| W3-11 | Analytics beacon | LOW (pre-existing) | No per-IP rate limit; client-supplied `created_at`; unbounded `properties` size |
| W3-12 | Gift cards | INFO | `IntegrityError` conflation between code-collision and already-issued (negligible probability) |
| W3-13 | Badges | INFO (accepted) | `accepted_families` 300 s platform-wide cache staleness after disabling a processor |

---

## 1. Gift-card campaigns (T045 / ADR-029) — the money gate

Checklist per the ADR's own Safety-gate section, plus the eight audit points.

### 1.1 Forged-PAID surface (H1 lesson) — PASS

Traced both processors:

- **Stripe** (`payments/webhook_views.py::_handle_payment_intent_succeeded`):
  signature verification (per-account `webhook_secret`) → H1 account binding
  (`_order_bound_to_account`, including the "no pinned account = refuse"
  case) → **amount_received + currency check against `Order.total` BEFORE the
  PAID transition** → PAID → `transaction.on_commit(_enqueue_gift_card_campaigns)`
  strictly inside the `if not already_paid:` branch.
- **PayPal** (`payments/paypal_webhook_views.py::_handle_capture_completed`):
  signature verification → H1 binding → Decimal-safe amount/currency check
  **before** the PAID transition → PAID → same `on_commit` enqueue.

The upsell promotion path (`_promote_upsell_charge_captured`) does **not**
enqueue issuance (correct — upsell captures are not the original PAID
transition), and rejects internal `upsell:` placeholder ids. A forged or
amount-mismatched capture cannot reach the enqueue on either processor. The
task itself re-checks `payment_status == PAID` and `order.total > 0` (D4
step 1-2) rather than trusting the enqueue site.

### 1.2 Code entropy + enumeration — PASS

`discounts/service.py::_generate_unique_gift_card_code`:
`secrets.token_hex(8).upper()` = **64 bits** from the CSPRNG (≥ the 60-bit
ADR floor), 16 uppercase hex chars matching the admin-020 format;
pre-checked for uniqueness per store and backed by the `('store', 'code')`
unique constraint. Online enumeration of a 2^64 space is infeasible;
redemption-endpoint rate limiting is the existing checkout surface
(out of wave-3 scope, previously audited).

### 1.3 Idempotency under webhook replay — PASS, one edge (W3-6)

The axis is `campaign_id = f"gift_card_campaign:{campaign.pk}:order:{order.pk}"`
under the unique `(store, campaign_id, recipient_email)`. Replayed webhook →
same enqueue path (and Stripe/PayPal both skip the enqueue entirely on
duplicate delivery since the order is already PAID) → same `campaign_id` →
`IntegrityError` → no-op, no email. Proven by
`test_running_task_twice_for_same_order_issues_exactly_once`,
`test_*_duplicate_delivery_does_not_re_enqueue`, and the concurrency test.

- **W3-6 (LOW):** because `recipient_email` is part of the unique key, a
  replay that observes a *changed* `order.customer_email` (admin edit between
  deliveries, task retry after a Celery delay) inserts a second reward row
  and issues a second card — the rows do not collide. Recommend an explicit
  pre-check on `campaign_id` alone (`CampaignReward.objects.for_store(...)
  .filter(campaign_id=...).exists()`) inside the campaign-row lock, or moving
  the email out of the uniqueness axis for this namespace.

### 1.4 Self-gifting brake — PASS (by-design residual noted in W3-4)

`Order.total` is computed post-coupon/post-gift-card at checkout
(`cart/checkout.py::compute totals`; fully-covered orders take the
ZERO_TOTAL path that never enqueues the task), and the task independently
skips `order.total <= 0`. A 99 %-gift-card order **does** still trigger —
deliberate per D9 ("the buyer injected new money"), with the frequency cap
as the second brake. Tests:
`test_fully_gift_card_paid_order_never_triggers_issuance`,
`test_partially_gift_card_paid_order_still_triggers`.

**Residual (W3-4, MEDIUM accepted-risk):** the issued card's value is not
bounded by the processor-charged amount, `cap_enabled` defaults to False and
`max_issued_cards` defaults to unlimited. On an `all_products` campaign, N
minimum-value orders mint N full-value cards (the 765×$5 shape, now
idempotent per order but still linear in orders). ADR-029 D5 mandates that
"the admin form help text recommends enabling it (e.g. 1 card / 30 days)" —
the `GiftCardCampaignAdmin` "Frequency cap" fieldset (`discounts/admin.py`)
**has no such description**. Add the D5 help text (and consider a non-null
default for `max_issued_cards` on publish) before release.

### 1.5 Caps under concurrency — PASS

`issue_campaign_reward_for_order` takes `SELECT FOR UPDATE` on the campaign
row **first**, then evaluates the budget cap (`issued_count >=
max_issued_cards`), the frequency cap (reward-row count in the sliding
window, prefix `gift_card_campaign:{pk}:`), the publish re-check, and the
expiry re-check — all inside the lock; the DiscountCode+CampaignReward pair
is created in a nested savepoint before the lock releases. Serialization is
correct. Proven by
`test_concurrent_orders_same_email_never_over_issue_under_cap` and
`test_budget_cap_boundary`.

Note (from the ADR's own risk list, for the Developer/Release Manager): the
frequency-cap `startswith` query on Postgres may need a
`varchar_pattern_ops` index on `campaign_id` under non-C collation — verify
the query plan in the production environment; not a security issue.

### 1.6 Admin-authored email content — PASS with W3-5

- **Header injection:** `subject_override` is rendered then `.strip()`ed;
  embedded CR/LF would survive into the subject **but** Django's mail layer
  (`forbid_multi_line_headers`) raises `BadHeaderError` on any newline in a
  header, which `send_transactional_email` catches → FAILED `SentEmail` row,
  no send. Not exploitable.
- **HTML injection:** `email_body` is admin-authored HTML delivered into the
  transactional wrapper — by design, identical to the `EmailTemplate`
  posture (store-admin-authored HTML emails). Token pills are literal-text
  replaced to `{{ gift_value }}` / `{{ gift_code }}` before rendering.
- **W3-5 (LOW, re-review trigger):** both overrides are rendered via
  `engines['django'].from_string(...)` with a context containing the **full
  `order` model instance** (and `store` added by the send path). Django
  templates cannot execute code, but attribute traversal from `order` /
  `store` can reach related objects and non-secret-looking sensitive fields.
  In v1 only super-admins author campaigns, and StoreOwnedModel related
  managers raise on unscoped traversal, so exposure is minimal and
  consistent with the existing EmailTemplate posture. **Hard requirement:**
  before `owner_scope='store'` campaigns ever ship (the field deliberately
  keeps that door open), this context must be flattened to plain strings
  (`order_number`, `customer_name`, …) — record this in the follow-up
  ticket. Cheap hardening now: pass primitives instead of the `order`
  instance for this template family.

### 1.7 Refund path — **W3-1 (MEDIUM, fix before release)**

What is correct:

- **Account binding on both refund handlers** (the PayPal M2 lesson):
  Stripe `_handle_charge_refunded` and PayPal `_handle_capture_reversed`
  both run `_order_bound_to_account` before acting — a forged refund event
  signed with another tenant's secret cannot void cards or flip order state
  cross-store. Covered by `test_webhook_account_binding.py`.
- Partial Stripe refunds are excluded from voiding (D9 v1); PayPal
  `CAPTURE.REVERSED` is inherently full.
- `void_or_flag_campaign_gift_cards_for_refund` is correct: unspent →
  `is_active=False`; spent → `refund_flagged` only, never clawed back; the
  `endswith=':order:{pk}'` match cannot false-positive across order pks
  (the leading `:` prevents suffix aliasing); `gift_card_auto`-only filter
  protects manual cards. All five D9 service tests pass.

The gap — **the REFUNDED transition has no state whitelist, and the PAID
handlers do not refuse REFUNDED** (the exact CANCELLED→PAID / MEDIUM-2
lesson, not extended to the new transition):

1. `_handle_charge_refunded` (Stripe) transitions **any** non-REFUNDED
   status (PENDING, FAILED, CANCELLED) to REFUNDED.
   `_handle_capture_reversed` (PayPal) does the same — its own docstring
   says "Only transitions from PAID", but no such guard exists in the code.
2. Worse, the inverse: **neither PAID handler refuses a REFUNDED order.**
   Stripe's `_handle_payment_intent_succeeded` refuses only CANCELLED;
   PayPal's `_handle_capture_completed` likewise. Webhook delivery order is
   not guaranteed; a fast pay-then-refund (fraud-triage refunds, instant
   refunds) delivered out of order does: `charge.refunded` first →
   PENDING→REFUNDED, `void_or_flag…` runs (no cards exist yet, no-op) →
   `payment_intent.succeeded` second → **REFUNDED→PAID**, volume accrued,
   purchase analytics fired, **gift cards issued for an already-refunded
   order and never voided** (the void hook already ran). This is a direct
   money-liability leak through event ordering, no forgery required.

**Required fix (small):** an explicit allowed-transitions whitelist —
REFUNDED only from PAID (log-and-flag otherwise, same manual-reconciliation
posture as MEDIUM-2), and both PAID handlers refuse REFUNDED exactly as
they refuse CANCELLED. **Required tests (After-Bug agent):** out-of-order
refunded-then-succeeded on both processors ⇒ no PAID flip, no issuance;
PENDING order receiving charge.refunded ⇒ no REFUNDED flip. None of the ten
tests in `payments/tests/test_gift_card_campaign_webhooks.py` cover
ordering today.

### 1.8 FR floor — W3-7 (LOW)

The floor **is** in model-level validation (not form-only):
`GiftCardCampaign.clean()` → `discounts/validators.py::
validate_gift_card_campaign` (single §XV-5 validator), FR targeting derived
from `StoreLanguage`/`ShippingCountry`, floor
`GIFT_CARD_FR_MIN_VALIDITY_DAYS` (1826) for both expiry modes at publish
time. Remaining gaps, all LOW given the super-admin-only v1 surface:

- F1 class: `clean()` is not invoked by `.save()`/`queryset.update()`; any
  future programmatic publish path bypasses the floor. (The only v1 write
  path is the admin form, which calls `full_clean()`.)
- No issuance-time re-check: a campaign published before the store added an
  FR market keeps issuing short-expiry cards; equally, editing a published
  campaign only re-validates on the admin path.
- Recommend: cheap `_store_targets_fr` re-check inside
  `issue_campaign_reward_for_order` (already under the row lock, one
  query), clamping `ends_at` to the floor with a warning log.

### 1.9 Other

- **W3-8 (LOW):** `GiftCardCampaignAdmin.formfield_for_manytomany` offers
  `Product.objects.cross_store_unsafe()` and neither the form nor
  `save_related` validates `trigger_products ⊆ campaign.store`. A
  super-admin can attach store B's products to store A's campaign; the
  runtime reads products via `for_store(campaign.store)` so mismatched rows
  **silently never match** — a §XV-1 invisible failure (operator confusion,
  not privilege escalation). Validate store consistency in
  `GiftCardCampaignAdminForm.clean()`.
- **W3-12 (INFO):** inside the nested savepoint, an `IntegrityError` from a
  `(store, code)` collision would be misread as "already issued" and drop
  the issuance for that order. At 64-bit entropy this is negligible;
  documenting so nobody "optimizes" the code-uniqueness pre-check away.
- Processor credentials in logs: reviewed all new/changed log lines in both
  webhook modules and `discounts/service.py` — ids are fingerprinted
  (`_reference_fingerprint`), no secret material, no full code values in
  webhook logs (service logs print the gift-card code on refund
  void/flag — visible to log readers; acceptable for ops, codes are
  deactivated/flagged at that point, but consider masking to last-4).

---

## 2. Page versions (T042 / ADR-028)

### 2.1 ATC hidden field — PASS

`cart/views.py::_validate_page_version_id` implements the safety-gate
requirement exactly: `int()` coercion, then
`ProductPageVersion.objects.for_store(request.store).filter(pk=…,
product_id=…)` existence check derived from the **variant's** product (not
a client-supplied product id); mismatch/malformed → `None` (never trusted
into the stamp). Cross-product, non-existent and cross-store ids all fall
to NULL. Covered by `cart/tests/test_page_version_id_validation.py`;
first-touch stamp semantics and the order-copy covered by the stamp tests.
`OrderItem.page_version_id` is a soft int, never joined for money display.

### 2.2 Beacon ingest — **W3-2 (MEDIUM, fix before release)** + W3-3

The beacon endpoint itself keeps its prior posture (store from Host header
only, body `store_id` ignored, closed event-type vocabulary, ≤50
events/request, parameterized SQL — no injection). But ADR-028's gate item
says: *"beacon `properties.page_version_id` … must be validated/coerced to
int server-side before raw-SQL insert."* **This was not implemented.**
`analytics/ingest.py` stores `properties` verbatim (`json.dumps` of
whatever dict arrived); the int coercion exists only as a **read-time SQL
cast** in `analytics/management/commands/aggregate_metrics.py::
_json_int_expr` — `CAST((properties->>'page_version_id') AS INTEGER)`.

- On **PostgreSQL (production, per `webecom/settings/production.py`)** that
  cast **raises** for any non-integer value an anonymous visitor can send
  (`"abc"`, `true`, `1.5`, a 2^70 int). One crafted `page_view` event with a
  non-null `product_id` makes `_query_page_version_metrics` — and therefore
  `_run_day`, which has no try/except — fail for that calendar day, for
  **every store** (the query is not store-partitioned), repeatably for the
  whole 90-day retention of the poisoned row. A one-request, unauthenticated,
  persistent DoS of the analytics pipeline.
- On **SQLite (the test settings)** `CAST` is lenient (returns 0), which is
  why the 993-test run stays green — SQLite green is not evidence here.

**Required fix:** coerce at ingest (whitelist `properties.page_version_id`
to `int` or drop the key; while there, coerce top-level
`product_id`/`collection_id`/`order_id` to int-or-NULL — they are also
passed raw), **and** defensively make the SQL expression non-throwing on
Postgres (e.g. regex-guarded `CASE WHEN properties->>'page_version_id' ~
'^[0-9]{1,9}$' THEN … END`) plus a per-day try/except in `handle` so one
bad day cannot stop the loop. **Required test:** a Postgres-semantics
regression (or at minimum an ingest-coercion unit test proving non-int
values never reach the DB).

**W3-3 (MEDIUM):** `page_version_views`/`page_version_atc` are deliberately
**not top-N capped**, and both `product_id` and `page_version_id` are
unvalidated public input — 50 events/request with fabricated distinct
`(product_id, page_version_id)` pairs mint unlimited daily
`AggregatedMetric` rows in the **main** DB. Once ingest coercion (W3-2)
lands, additionally bound cardinality: aggregate only pv_ids that exist (or
existed) for the store, or cap distinct dimension keys per store/day.

**W3-11 (LOW, pre-existing, amplified):** no per-IP throttle on
`/analytics/beacon/` (only the 50-event cap); `created_at` is accepted from
the client (backdating into already-aggregated days silently diverges raw
vs aggregated data; future-dating games the purge); `properties` size is
unbounded per event. Recommend: reverse-proxy rate limit, clamp
`created_at` to `now ± small skew`, cap serialized `properties` length.

### 2.3 Admin CRUD + report — PASS

- `ProductPageVersionAdmin` (store site only): queryset `for_store`,
  product dropdown `for_store`, `save_model` forces
  `obj.store = obj.product.store` (no store-switching), gated on the
  `products` module (limited=view / full=write). The image inline's FK
  queryset is restricted to the parent product's images and Django's
  `ModelChoiceField` validates the *submitted* pk against that queryset, so
  a tampered POST with another product's/store's `ProductImage` pk fails
  form validation; `ProductPageVersionImage.clean()` re-checks
  same-product as defense in depth. Cap-10 and reserved-slug composition in
  `clean()` (surface as form errors, not 500s).
- Report view (`page_versions_report_view`): wrapped in
  `admin_site.admin_view` (auth) **and** `_check_module_access(request,
  "analytics", "limited")` per ADR-028 §7; product fetched `for_store`;
  `days` clamped to [1, 365]; `build_page_version_report` is ORM-only,
  store-scoped, and parses dimension keys defensively. Revenue comes from
  main-DB `OrderItem` stamps, not beacon data — beacon poisoning (W3-3)
  can inflate views/ATC but never orders/revenue.
- Cosmetic: `page_versions_html` hardcodes the changelist URL
  (`/admin/catalog/productpageversion/...`) instead of `reverse()` — breaks
  if the admin is remounted; not a security issue.

---

## 3. Security badges (T046 / ADR-030)

### 3.1 Upload path — PASS with W3-10

- Raster-only enforcement is layered: (1) `validate_custom_image_not_svg`
  field-local allow-list (PNG/JPEG/WebP) independent of the global
  `ALLOWED_IMAGE_TYPES`, with the FieldFile `.file` drill-down so model
  `full_clean()` re-validation sees the content type; (2) the admin
  `ModelForm`'s `forms.ImageField` runs **Pillow** verification — a real
  SVG cannot pass regardless of a crafted `Content-Type` (Pillow cannot
  open SVG), and the default `validate_image_file_extension` restricts the
  stored extension to Pillow-known image extensions (no `.html`/`.svg`
  stored names via the admin). Content-type spoofing therefore does not
  yield an SVG/HTML file at a served path through the only existing write
  path (the store-admin form). Size cap: `MAX_IMAGE_UPLOAD_MB` (10 MB)
  via `validate_image_file`.
- **W3-10 (LOW / deployment):** residual risk is polyglot rasters (valid
  PNG with embedded markup) served from `/media/` and any *future*
  programmatic save path (model validators don't run on `.save()`).
  Hardening: (a) add `FileExtensionValidator(["png","jpg","jpeg","webp"])`
  to the model field so the guarantee doesn't depend on the form layer;
  (b) production must serve `/media/` with `X-Content-Type-Options:
  nosniff` and extension-derived Content-Type (add to the deployment
  checklist — production `MEDIA_URL`/storage is still the commented-out S3
  block in `settings/production.py`, so this will be configured at deploy
  time).

### 3.2 `heading_segments_json` / `locations_json` — PASS with W3-9

- Heading segments: strict shape validation in `_validate_heading_segments`
  (list ≤6, non-empty `text` ≤40 chars, `color` matching
  `^#[0-9a-fA-F]{6}$`) and rendered **only** through the auto-escaping
  template path (`_heading.html`; `mark_safe` is applied to the joined
  template *outputs*, never to row data). The XSS regression test the ADR
  demanded exists and passes
  (`test_heading_segment_html_is_escaped_in_output`). Store-admin-authored
  text cannot break out of the span/style context even if validation were
  bypassed, because autoescape covers `"`, `<`, `>` in both text and
  attribute positions.
- Locations: keys restricted to `TrustBadgeLocation.values`, `width_px`
  strictly `int` (bool excluded) in [100, 800] — no unit/CSS injection via
  the `style="width: {{ width_px }}px"` sink.
- **W3-9 (LOW):** `border_color` (`CharField(max_length=7)`) is rendered
  into the same style attribute but has **no** hex validation in `clean()`.
  Autoescape plus the 7-char cap make it practically inert (worst case:
  broken CSS on the store's own page), but it is inconsistent — validate it
  with the same `HEX_COLOR_RE`.

### 3.3 Truthfulness gates as security properties — PASS with W3-13

- `is_secure`: `lock_shield`/`ssl_seal` render only under
  `request.is_secure()`, gated in the provider/templates, not by admin
  config — verified in all four preset templates; negative test
  `test_lock_icon_never_renders_on_plain_http` passes.
- `accepted_families`: derived from `PaymentMethod.is_enabled` at render
  time (never a fixed list); card/paypal negative tests pass. Cross-store:
  provider scopes rows `for_store` and returns "" without a store;
  `test_other_store_badge_not_rendered` passes. Row isolation: one row's
  template error cannot take down the slot (matches Pixels precedent).
- **W3-13 (INFO, accepted per ADR §3):** the 300 s platform-wide cache
  (`security_badge:accepted_families`) means a *disabled* processor family
  keeps being advertised for up to 5 minutes, and the derivation is
  platform-level, so a store whose own org has no healthy account for an
  enabled family still shows that logo — both explicitly accepted in the
  ADR as decorative-only inaccuracy. Cheap improvement: delete the cache
  key from a `PaymentMethod` `post_save`/admin `save_model` hook.
- Admin gating: dedicated `security_badges` module key, full-only, on all
  five permission hooks — matches AF-002; `is_super_admin` bypass is the
  platform convention.

---

## 4. Human checklist before release

1. Apply and regression-test the W3-1 transition whitelist (both
   processors) and the W3-2 ingest coercion — both are small, contained
   patches; route to Developer + After-Bug Test agent.
2. Confirm production `/media/` serving config sets
   `X-Content-Type-Options: nosniff` (W3-10) when the storage backend is
   finalized.
3. On the production Postgres, `EXPLAIN` the frequency-cap
   `campaign_id LIKE 'gift_card_campaign:N:%'` query; add
   `varchar_pattern_ops` index if it seq-scans (ADR-029 risk note).
4. Decide the W3-4 posture: keep caps opt-in (then at minimum add the D5
   help text and an ops alert on `outstanding_count` growth) or default
   `max_issued_cards` on publish.
5. Track W3-5's re-review as an acceptance criterion on any future
   `owner_scope='store'` gift-card-campaign ticket.
6. The tracked M2M read-path IsolationError must be fixed (or the change
   views feature-flagged) before merchants/operators need the
   `DiscountCode`/`GiftCardCampaign` change forms — Release Manager
   dependency, owned by the Architect.

---

## Verdicts

| Area | Verdict |
|---|---|
| **Gift-card campaigns (T045/ADR-029)** | **APPROVED WITH CONDITIONS** — condition: W3-1 (REFUNDED transition whitelist + PAID-handler REFUNDED refusal, with regression tests) and the W3-4 D5 help-text/mitigation decision. Everything else in the money path (forged-PAID guards, entropy, idempotency, locking, refund binding, void/flag policy) is solid and well-tested. |
| **Page versions (T042/ADR-028)** | **APPROVED WITH CONDITIONS** — condition: W3-2 (ingest coercion + non-throwing aggregation; this is the ADR's own named safety-gate item and is currently unmet) and a W3-3 cardinality bound. The ATC field, admin CRUD, and report gating are correct as shipped. |
| **Security badges (T046/ADR-030)** | **APPROVED** — W3-9/W3-10 are low-severity hardening items that may land post-release; the XSS, upload, truthfulness, isolation, and permission surfaces all check out with real test coverage. |
| **Overall wave-3** | **APPROVED WITH CONDITIONS** — release after the two named MEDIUM fixes (W3-1, W3-2) land with proven regression tests; remaining items are scheduled hardening. |

---

# Re-verification and final verdict (Safety Agent, 2026-07-11)

Both release conditions plus the two secondary items were re-verified
**against current code**, not just the fix reports.

**Test evidence (fresh runs, this re-verification):**
`payments.tests.test_refund_ordering` + `analytics.tests.test_w3_2_beacon_coercion`
= 34 tests OK · audited apps (`discounts payments badges catalog analytics
cart`) = 1071 tests OK (was 993 at the original audit) · full suite = **3437
tests OK**. BUG_TESTS ledger rows `W3-1-REFUND-ORDERING` and
`W3-2-BEACON-COERCION` are **PROVEN** with documented one-at-a-time
revert→fail→restore→pass cycles per guard/part.

## W3-1 — CLOSED (verified in code)

All four guards present, in the correct positions, with the correct
warn-and-return posture (never raise — webhook 200 preserved):

1. `payments/webhook_views.py::_handle_payment_intent_succeeded` — refuses
   REFUNDED (`payments.webhook.refunded_order_payment`) after the CANCELLED
   guard, **before** the PAID transition / volume accrual / analytics /
   gift-card enqueue.
2. `payments/webhook_views.py::_handle_charge_refunded` — PAID-only
   whitelist (`payments.webhook.refund_before_paid`) placed **after** the
   idempotent already-REFUNDED skip, so duplicate refund deliveries stay
   silent while premature refunds are refused loudly.
3. `payments/paypal_webhook_views.py::_handle_capture_completed` — same
   REFUNDED refusal, before the amount check and PAID transition.
4. `payments/paypal_webhook_views.py::_handle_capture_reversed` — PAID-only
   whitelist, closing the docstring/code mismatch the audit flagged.

The four regression tests assert not just the log line but the absence of
the status flip, of `Organization.current_month_volume` accrual, and of any
call to the `_enqueue_gift_card_campaigns` seam. The legitimate PAID→REFUNDED
+ void path remains covered by the pre-existing
`test_gift_card_campaign_webhooks.py` / `test_paypal.py` tests. The
REFUNDED→PAID gift-card minting leak is closed on both processors.

## W3-2 — CLOSED (verified in code, all three layers)

1. **Ingest coercion** (`analytics/ingest.py::_coerce_int_or_none`):
   rejects bool (the `isinstance(True, int)` trap is explicitly handled),
   non-integer floats, non-digit strings, and anything outside signed
   32-bit; applied to `properties.page_version_id` (garbage is **dropped
   from the stored JSON**, never persisted) and to the top-level
   `product_id`/`collection_id`/`order_id` soft ids (garbage → NULL) — the
   audit's "while there" recommendation included. The proof cycle even
   surfaced that a 2^70 int previously killed the **entire batch** via
   OverflowError inside the ingest try/except — also fixed by the clamp.
2. **Non-throwing SQL** (`aggregate_metrics.py::_json_int_expr`, Postgres
   branch): `CASE WHEN (…->>'…') ~ '^[0-9]{1,9}$' THEN CAST(…) ELSE NULL
   END` — cannot raise and cannot overflow, and defends pre-fix rows still
   inside the 90-day retention window. SQLite branch unchanged (it was
   never the throwing side).
3. **Per-day containment** (`Command.handle`): each `_run_day` wrapped in
   try/except with failed-day reporting — one poisoned day can no longer
   abort a `--days N` run.

Residual footnote (INFO, no action required): a *negative* int passes
ingest coercion but maps to NULL→0 at aggregation, folding into the primary
baseline row — pollution equivalent to any fabricated id and now bounded by
the W3-3 cap.

## W3-3 / W3-4 — CLOSED (verified in code)

- `PAGE_VERSION_TOP_N = getattr(settings, 'ANALYTICS_PAGE_VERSION_TOP_N',
  500)` applied via `_top_n` to **both** `page_version_views` and
  `page_version_atc` — dimension cardinality from public input is now
  bounded per store/day/type.
- The ADR-029 D5 help text now exists on **both** the "Frequency cap" and
  "Budget cap" fieldsets in `discounts/admin.py`, naming the self-gifting
  loop and the unbounded-liability default explicitly.

## Noted, not re-audited (per coordinator)

The ADR-031 chain closed the three StoreOwnedModel isolation rings (M2M
read path — the issue this audit carried as "tracked"; formset PK +
terminal methods including the silent-update hole; validation window +
admin field scoping). The former Release-Manager dependency on the
`DiscountCode`/`GiftCardCampaign` change views is therefore lifted. Suite
green at 3437 confirms no regression across the wave-3 surfaces.

## Still open (unchanged, none release-blocking)

- **LOW/INFO items W3-5 through W3-13** — scheduled hardening as listed;
  in particular W3-5's hard requirement stands: flatten the email-template
  context to primitives before any `owner_scope='store'` gift-card
  campaign ships.
- **Deploy-time checklist items** (§4): `/media/`
  `X-Content-Type-Options: nosniff` when the production storage backend is
  finalized, and the `EXPLAIN` of the frequency-cap prefix query on
  production Postgres (`varchar_pattern_ops` if it seq-scans). These are
  environment tasks, not code tasks — hand to the Release Manager's
  deployment checklist.

## Final verdicts (supersede the table above)

| Area | Verdict |
|---|---|
| Gift-card campaigns (T045/ADR-029) | **APPROVED** — W3-1 closed and proven; W3-4 mitigation in place. |
| Page versions (T042/ADR-028) | **APPROVED** — W3-2 closed and proven at all three layers; W3-3 bounded. |
| Security badges (T046/ADR-030) | **APPROVED** (unchanged). |
| **Overall wave-3** | **APPROVED** — both release conditions met with PROVEN regression tests; remaining LOW items and the two deploy-time checklist entries transfer to the Release Manager. |
