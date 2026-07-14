# Release Notes — Wave 3: Page Versions, Gift-Card Campaigns, Security Badges + Isolation-Hardening Chain

**Date:** 2026-07-11
**Tickets:** TICKET-042 (page versions), TICKET-045 (gift-card campaigns), TICKET-046
(security badges), TICKET-050/051/052/053 (isolation-hardening chain)
**ADRs:** ADR-028 (page versions), ADR-029 (gift-card campaigns), ADR-030 (security badges),
ADR-031 + 2 addenda (relation-bound M2M reads / formset PK + terminal methods / validation
window), ADR-032 (admin unique validation)

---

## What shipped

### A/B multi-variant product pages (TICKET-042 / ADR-028)
- A product may have up to 10 alternate "page versions" — same product, price, SKU/variants,
  and translated text, **different image set only** — each served at its own URL
  (`/<product-slug>-<suffix>/`), registered per language in the Permalink table.
- Canonicalized to the primary product URL, excluded from sitemap/hreflang/feeds by design
  (link-level traffic split only — no automatic assignment, no consent-cookie surface). A soft
  `page_version_id` stamp is carried through `Event.properties`/`CartItem`/`OrderItem` for
  per-version view/ATC/order reporting.
- Deactivating a version 302-redirects its URL(s) to the primary (reversible); deleting a
  version 301-redirects (permanent, preserves inbound link equity from ad/Pinterest traffic).
- Admin CRUD (`ProductPageVersionAdmin`) and a per-product report view, both store-scoped and
  permission-gated on the existing `analytics`/`products` module hooks.

### Automated gift-card campaigns (TICKET-045 / ADR-029)
- New `discounts.GiftCardCampaign` model: `order_paid`-triggered (v1; `nth_order`/
  `spend_threshold`/`win_back` reserved for later), **fixed-value only** in v1, issued via a
  post-PAID-commit Celery task made idempotent by the existing `CampaignReward` pattern plus a
  campaign-row `SELECT FOR UPDATE` serializing budget/frequency-cap checks under concurrency.
- Email-only delivery; admin-authored subject/body support token substitution
  (`{{ gift_value }}`/`{{ gift_code }}`).
- 5-year French statutory floor on expiry (configurable validity otherwise, based on the
  store's markets); refund policy: unspent codes are deactivated, spent codes are flagged for
  manual review (never clawed back); self-gifting brake: zero-processor-charge-amount orders
  never trigger issuance, plus a per-recipient-email frequency cap.
- Replaces the legacy platform's own automated gift-card campaign mechanism — this feature is
  the answer to the "what auto-issued the 765 uniform $5 cards" mystery flagged during spec
  work, not a new liability surface invented from scratch.

### Security badge designer (TICKET-046 / ADR-030)
- New `badges.SecurityBadge` model with a built-in preset registry
  (`badges/badge_presets.py`, same code-registry shape as the existing overlay-theme registry):
  self-verifying lock/SSL-seal icon (gated on `request.is_secure()`) and payment-network logos
  **derived from the store's actually-enabled `PaymentMethod.method_family` rows** — never a
  fixed logo list.
- **Deliberately does not ship any third-party certification marks** ("McAfee Secured"/
  "Norton Secured"/TRUSTe/BBB/etc.) as built-in assets, because the platform cannot verify any
  store's actual enrollment in such a service and shipping the mark anyway would be a false
  certification claim. Stores may still upload their own image to display a certification they
  genuinely hold.
- Renders into the already-existing `slot.security_badge` slot on product/cart/checkout — all
  three locations ship in v1 (one-line addition wires the slot into `cart.html`, which did not
  call it before).
- Live re-render preview pane is explicitly out of scope for v1 (PENDING follow-up ticket).

### Isolation-hardening chain (TICKET-050/051/052/053, ADR-031 + 3 addenda, ADR-032)
This chain started as a hotfix for two admin change views broken by the store-isolation guard
and ended up closing a genuine **silent cross-store write** hole. In order:
- **TICKET-050 (ADR-031):** M2M relation-bound managers (e.g. `campaign.trigger_products.all()`)
  get a real queryset instead of raising, paired with a new `core/m2m_guard.py` `m2m_changed`
  pre_add guard that raises `IsolationError` on any cross-store M2M row — closing the
  `DiscountCode`/`GiftCardCampaign` admin change-view crash without weakening isolation.
- **TICKET-051 (ADR-031 addendum):** admin formset hidden-pk lookups (`BaseModelFormSet.add_fields`)
  were using the raw, unscoped manager — fixed via `core/formsets.py`'s
  `StoreSafePKFormSetMixin`. Separately, **`_RaisingQuerySet` was found to only guard
  iteration/len/bool/fetch** — `aggregate`, `count`, `exists`, `contains`, `iterator`,
  `aiterator`, `update`, `bulk_update`, `delete`, and `explain` all executed SQL directly and
  bypassed the guard entirely. The worst of these, **`update()`, was a silent, unscoped
  cross-store write** reachable from any code that chained `.filter(...).update(...)` on a
  `StoreOwnedModel` manager without going through `.for_store()`. All of these terminal methods
  now raise unconditionally (with `Model.objects.none()` added as the one sanctioned safe
  empty-queryset entry point).
- **TICKET-052 (ADR-031 Addendum 3):** closing the terminal methods above turned the tree red
  (27/28 failures) because Django's own `_perform_unique_checks`/`_perform_date_checks`/
  `UniqueConstraint.validate` had only ever worked via the now-closed silent-`exists()` hole —
  every `full_clean()`/admin `is_valid()` call on any `StoreOwnedModel` with a unique constraint
  was affected. Fixed via a narrow contextvar validation window that unlocks only `exists()`
  for the duration of Django's own constraint checks, plus `StoreScopedFormFieldsMixin` scoping
  every FK/M2M admin widget queryset by construction.
- **TICKET-053 (ADR-032):** the "store excluded from the form, set in `save_model`" admin
  convention used across the codebase turned out to defeat friendly unique validation through
  two independent Django 6.0.4 mechanisms even after TICKET-052's fix, so a same-store duplicate
  through any store-site admin's real HTTP endpoint still 500'd with a raw `IntegrityError`.
  Fixed by `StoreUniqueValidationFormMixin` (injects the instance's store before Django's
  `_post_clean()` and un-excludes it from validation), wired by construction through the same
  form/formset machinery every admin already goes through — zero per-admin edits required. This
  ticket also folded in the `CsvListWidget` display **and data-corruption** fix: an empty
  `tags`/`coverage_areas_json` field previously round-tripped as the literal string `'[]'` on
  every no-op save; it now round-trips as `[]` as intended, with `blank=True` added to both
  model fields and a migration to match.

**Say this prominently, as instructed:** the isolation chain closed a **silent cross-store
write hole** — an unscoped `.update()` on a `StoreOwnedModel` queryset executed the SQL directly
without ever raising, meaning a coding mistake anywhere in the codebase that reached this path
could have silently written to another store's rows. This is now closed and proven
(BUG_TESTS.csv `TERMINAL-METHODS-ISOLATION`, STATUS=PROVEN), independently re-confirmed in code
by this release pass (`core/managers.py::_RaisingQuerySet.update()`).

## Notable fixes folded into this release
- **W3-1 (Safety Agent, MEDIUM→CLOSED):** the REFUNDED payment-status transition had no
  whitelist, and neither processor's PAID webhook handler refused an already-REFUNDED order — an
  out-of-order webhook delivery (refund event before the success event) could flip
  REFUNDED→PAID, accrue volume, fire purchase analytics, and issue gift cards for an
  already-refunded order that would never be voided. Fixed on both Stripe and PayPal handlers;
  proven with dedicated ordering regression tests.
- **W3-2 (Safety Agent, MEDIUM→CLOSED):** the analytics beacon accepted
  `properties.page_version_id` (and top-level `product_id`/`collection_id`/`order_id`) without
  server-side coercion. On PostgreSQL (the real production database engine — this was
  invisible under the test suite's SQLite, which casts leniently), a single crafted, unauthenticated
  beacon event could crash the daily aggregation job for **every store** for the entire 90-day
  retention window of the poisoned row. Fixed at three layers: ingest-time coercion (garbage
  dropped, never persisted), non-throwing SQL on the read side, and per-day exception
  containment in the aggregation command so one bad day can no longer abort a multi-day run.
- **W3-3/W3-4:** `AggregatedMetric` dimension cardinality from unvalidated public input is now
  capped (`ANALYTICS_PAGE_VERSION_TOP_N`, default 500 per store/day/type); the ADR-029 D5
  frequency-cap/budget-cap admin help text (naming the self-gifting-loop risk explicitly) now
  exists on both relevant fieldsets.
- **SEO FIX-1/2/3 (all APPLIED):** JSON-LD `offers.url` on a page-version render now points at
  the primary URL (was pointing at itself — the one on-page signal that disagreed with the
  canonical tag); two test gaps closed (fr-prefixed canonical HTML assertion, no-robots-meta
  assertion on version pages).

## What did not ship / was deliberately deferred
- Live re-render preview pane for security badges (ADR-030) — PENDING follow-up ticket.
- `nth_order`/`spend_threshold`/`win_back` gift-card trigger events, and
  `percent_of_order`/`percent_discount_coupon` value modes — reserved vocabulary, not built in
  v1 (ADR-029).
- Automatic significance testing / traffic-split assignment for page versions — link-level
  split only in v1 (ADR-028 P4).
- Store-authored gift-card campaigns (`owner_scope='store'`) — the field is deliberately kept
  open for a future ticket, but the email-template context flattening required before that
  ships (W3-5) has **not** been done yet; see `KNOWN_RISKS.md`.

## Dependencies
No new third-party dependencies were added for this release.

---

## Verdicts

| Area | Ready for staging | Ready for production | Conditions / owners |
|---|---|---|---|
| **Page versions (T042/ADR-028)** | Yes | Yes | None blocking. SEO PASS-WITH-FIXES, all 3 fixes applied and verified. Safety APPROVED, W3-2/W3-3 closed and verified. |
| **Gift-card campaigns (T045/ADR-029)** | Yes | Yes | None blocking. Safety APPROVED, W3-1/W3-4 closed and verified. **Hard requirement before any future `owner_scope='store'` campaign ships** (not blocking this release, since v1 only allows `owner_scope='platform'`): flatten the email-template context (W3-5) — owner: Developer, future ticket. |
| **Security badges (T046/ADR-030)** | Yes | Yes with 1 condition | Condition: payment-network SVG placeholder one-time legal/brand check before the *first real store* enables badges in production — owner: human/legal (ADR-030 Risk #1). Code-level: APPROVED outright, no blocking findings. |
| **Isolation-hardening chain (TICKET-050/051/052/053)** | Yes | Yes | None blocking — closed a real silent cross-store write hole, all 8 BUG_TESTS.csv rows PROVEN, 2 spot-checked directly in code by this pass. Former Release-Manager dependency (M2M read-path blocking admin change views) is lifted. **Do not roll back this chain independently of a full release rollback** — see `ROLLBACK_PLAN.md`. |
| **Overall wave 3** | **Yes** | **Yes**, subject to the deploy-time checklist in `KNOWN_RISKS.md` (media `nosniff`, frequency-cap `EXPLAIN`, real production `SECRET_KEY`) and the security-badges legal check above. | See `KNOWN_RISKS.md` for the full carried list with owners. |

## Overall project position (stated honestly, not just for this release)

Per the orchestrator's tracking: **47 of 53 tickets** are shipped as of this release (the four
product tickets in this wave plus the four isolation-chain tickets are counted in that 47).
Remaining, not part of this release and not blocking it:
- **T036** — Amazon FBA integration
- **T040** — Per-store custom email sender domain
- **T041** — Additional payment processors (HiPay, Mollie, BTCPay, BitPay)
- **T043** — French bookkeeping order export API
- **T044** — TimescaleDB analytics migration
- **T047** — Employee permission matrix — full admin UX
- Theme visuals (Designer pass) — not yet scheduled
- Staging deployment itself — has not yet happened; this release is READY to be deployed to
  staging, not already validated there

This wave closes out the isolation-layer hotfix chain and three product features; it does not
represent the end of the project backlog, and the Release Manager verdict above should not be
read as "the platform is feature-complete."
