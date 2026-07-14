# 09_architecture_decisions

> Architecture Decision Records — owned by Architect Agent.
> Binding inputs: `design-pattern-ideas.txt` (15 hard-won lessons, cited as §I–§XV), `02_feature_matrix.md`, DECIDED items in `11_uncertainties_to_validate.md`.

**Status: FIRST BATCH (ADR-001 … ADR-007) — 2026-07-02. REVISED 2026-07-02: ADR-001 (Store↔Organization relationship decided), ADR-004 (separate analytics DB for 500+ orders/day/store). REVISED 2026-07-03: ADR-007 (Safety Agent amendments — two-charge baseline, race guard, processor pinning, access control, provisional items, multi-charge refund model, ADR-005 gift-card open question).**

| ADR | Title | Status |
|---|---|---|
| ADR-001 | Multi-tenant store architecture | ACCEPTED (revised: Organization = payment-only + loose grouping) |
| ADR-002 | Discount engine — unified DiscountCode model | ACCEPTED |
| ADR-003 | AI Job System | ACCEPTED |
| ADR-004 | Analytics event tracking pipeline | ACCEPTED (revised: separate analytics DB, high volume) |
| ADR-005 | URL resolution and permalink model | ACCEPTED |
| ADR-006 | Payment routing — Control Plane architecture | ACCEPTED |
| ADR-007 | Upsell funnel state machine | ACCEPTED (Safety Agent review RESOLVED 2026-07-03 — two-charge baseline, 9 amendments applied) |
| ADR-008 | Multilingual domain→language→country model (`StoreDomain`/`StoreLanguage`/`ShippingCountry`, `resolve_locale`) — resolves ML-005 / 11:#9 — see `adr_008_multilingual_domains.md` | ACCEPTED (2026-07-03) |
| ADR-009 | Up-sell campaign data model — funnel graph ratified (`entry_step`), `owner_scope` precedence (store wins), `CampaignIssuedCode`→`DiscountCode` next-order codes, abandoned-checkout shares Campaign root — resolves ADR-007's 3 PENDING items (11 Part D, DECIDED 2026-07-04) — see `../../docs/adr/ADR-009-upsell-campaign-model.md` | ACCEPTED (2026-07-04) |
| ADR-010 | Abandoned-checkout campaign (T021) — Cart+`CampaignSession` detection (nullable `order`, new `cart` FK), dedicated `AbandonedCheckoutEmailStep`/`AbandonedCheckoutEmailSend` models, stateless signed resume token (30 d), global Celery beat scan, dual suppression (pre-cancel + at-send race guard) — see `../../docs/adr/ADR-010-abandoned-checkout-campaign.md` | ACCEPTED (2026-07-05) |
| ADR-011 | Up-sell capture-window payment flow (T028) — `OrderCharge` ratified in `orders` (+`CAPTURE_IN_PROGRESS`, claim timestamp, step soft-ref, partial uniques), `CampaignSessionToken` ratified in `campaigns`, `paypal_capture_mode` + `paypal_vault_enabled` on ProcessorAccount, PayPal Vault accept path (`intent=AUTHORIZE` → auth capture + vault-id merchant-initiated upsell order), watchdog `campaigns.tasks.capture_window_watchdog` every 2 min with stuck-capture reconciliation, `Campaign.capture_window_minutes` (default 10), DB-backed 5-attempt rate limit, additive migrations on orders/payments/campaigns — see `../../docs/adr/ADR-011-upsell-capture-window-payment-flow.md` | ACCEPTED (2026-07-05) |
| ADR-012 | Storefront theme system (T029) — DTL engine (zero new deps), 3 themes (`b2b`/`fashion`/`general` package names), all themes share one template set + DOM contract, `StoreThemeCustomization` split table (per-store overrides, not on platform `Theme` row), `SlotProvider` registry for T030/T034/T035/T023/T036/T028 slot hooks, 4-partial override whitelist (`hero`, `theme_band`, `product_badge`, `footer_band`), signed 2h preview token (noindex, no analytics), plain CSS custom properties + vanilla JS (no build step) — see `../../docs/adr/ADR-012-storefront-theme-system.md` | ACCEPTED (2026-07-05) |
| ADR-013 | Multi-step upsell funnel builder (T039) — session stays INTERACTION between steps, ORIGINAL captured exactly once (steps 2+ skip original capture), per-step single-use `upsell-act` token expiring at unchanged capture-window end, 5-attempt rate limit is session-wide, soft-fail ends funnel (CONVERTED if ≥1 prior captured, else DISMISSED), `CHARGEABLE_OFFER_TYPES` frozenset (coupon-only steps skip charge), `ChargeResult.processor_payment_intent_id` field, `payment_intent.amount_capturable_updated` webhook → PM id backfill, `payment_intent.succeeded` on UPSELL charge → CAPTURED — see `../../docs/adr/ADR-013-upsell-funnel-builder.md` | ACCEPTED (2026-07-05) |
| ADR-014 | Multilingual content pipeline (T024) — stable `VariantOption`/`VariantOptionValue`/`VariantOptionAssignment` entities replacing `option_values_json` (reversible migration), `manually_edited_at` + `source_fingerprint` on all translation records, new `VariantOptionTranslation`/`VariantOptionValueTranslation`/`ProductImageTranslation`, PK-keyed per-(product, lang) `options` sub-jobs (§VII), fingerprint-based auto-requeue skipping overridden records (P3/P4 opt 1), `run_ai_jobs` runner contract + real `translation` build_prompt/persist_output, **fix of §10-G1**: root-level product slugs + `products/` prefix data migration, admin coverage/override UX, option-value fallback = show source-language label — see `../../docs/adr/ADR-014-multilingual-content-pipeline.md` | ACCEPTED (2026-07-05) |
| ADR-024 | AI sales-assistant chat (T038) — resolves 11:#10 (DECIDED-human 2026-07-10) — **direct Claude API** for interactive chat (scoped carve-out from ADR-003 §7's batch-cost rule; batch `chat_store_digest` job stays on the CLI runner), tool-use over the live catalog + `StaticPage` policies (4 read-only tools, server-side store binding, no RAG/vector DB), new `chat` app (`ChatSession`/`ChatMessage`/`StoreChatSettings`), short-lived SSE per reply on plain WSGI (`?stream=0` fallback, no Channels), `claude-haiku-4-5` default via `CHAT_MODEL_ID` + per-store override, prompt caching (≥4096-token prefix), spend into `AiJobMetric` (`job_type='sales_chat'`) + `StoreAiQuota` with pre-call refusal, antispam reuse + per-session caps (30 msgs/150K tokens) + store daily budget + `CHAT_KILL_SWITCH`, **GDPR per-store owner opt-in OFF by default** (sub-processor acceptance stamped), 30-day retention purge, widget via ADR-012 chat slot (vanilla JS), **mandatory Safety Agent gate** — see `../../docs/adr/ADR-024-ai-sales-chat.md` | ACCEPTED (2026-07-10) |
| ADR-025 | GDPR/ePrivacy consent management (TICKET-048/049) — resolves the cross-backlog GDPR blocker (TH-141, ADR-022 D7, KNOWN_RISKS item 1) — new `consent` app (`ConsentSettings`/`ConsentRecord`), tri-state per-category consent (`necessary`/`analytics`/`marketing`) in one HttpOnly first-party cookie (`pradize_consent`, 180-day Max-Age = re-prompt interval) + `ConsentRecord` proof rows (13-month rolling purge), **server-side render gating** of `slot.pixels` by consent category with **consent-gated purchase/initiate claims at the claim site** (D0 correction to ADR-022 D7 — see ADR-022's dated addendum), reload-on-grant, CNIL-exempt first-party analytics beacon with an objection toggle (`am`) and a per-store conservative override (`beacon_requires_consent`), banner + preferences panel server-rendered into the existing `slot.consent` (ADR-012 D9, no new slot), per-store enable (`is_enabled`, default True) with a `non_eu_acknowledged` escape hatch wired into the launch checklist as a blocking error, platform `.po` strings (no per-store banner copy in v1) — all four P-items (banner copy, 180-day re-prompt, enabled-by-default rollout, 13-month retention) DECIDED (human, 2026-07-11); legal sign-off on the beacon-exemption reading remains an OPEN external item (`11_uncertainties_to_validate.md`) — see `../../docs/adr/ADR-025-consent-management.md` | ACCEPTED (human, 2026-07-11) |
| ADR-026 | Catalog feeds (T033) — new `feeds` app with `FeedProvider` registry (§IX; keys `google`/`facebook`, Pinterest = one class later), one RSS 2.0/`g:`-namespace renderer for both providers, **one item per active variant** with `id = pixels.events.catalog_item_id(product, variant)` (ADR-022 D4 freeze — same-function-object drift test NON-NEGOTIABLE) + `item_group_id = product.pk`, prices via `currency.resolver.display_amount()` in the feed country's display currency (FEED-008/FEED-C1) with new platform-global `COUNTRY_TO_CURRENCY` map + `feed_display_currency()` (missing rate → loud target ERROR, never silent store-currency fallback), links via permalink resolver (canonical primary only), **pre-generated** files (temp + `os.replace`) by 30-min debounced beat + nightly rebuild (FEED-005; deliberate divergence from sitemap's dynamic v1), explicit `FeedTarget` status machine with recorded per-product exclusion reasons (`no_image`/`quotation_mode`/`presale_no_date`/`no_translation` — AC-211/AC-212 availability matrix per inventory mode), per-store rotatable token URLs `/feeds/<provider>/<country>-<lang>.xml?token=…` (403 via `hmac.compare_digest`, AC-213) + `"feeds"` added to `RESERVED_TOP_LEVEL_SLUGS` (URL-001 gap fix), models `FeedConfig`/`StoreFeedToken`/`FeedTarget` `[S]`, `/admin/` cards with status banners + regenerate + token rotation, two tickets T033-A/T033-B, Safety + SEO gates named — **D4 field-mapping table signed off, all 9 PENDING product items (P-1…P-9) approved as recommended** — see `../../docs/adr/ADR-026-catalog-feeds.md` | ACCEPTED (human, 2026-07-11) |
| ADR-027 | Conversion overlays (T034 lead capture + T035 recent-purchase social proof) — new `engagement` app hosting both features via the ADR-012 slot registry (`slot.overlay`/`slot.social_proof`); `LeadCaptureCampaign`/`LeadSignup` (consent proof, no IP)/`LeadCaptureCampaignTranslation` `[S]`, antispam-reused signup POST (ADR-018 D3), idempotent shared-coupon issuance (`CampaignReward` pattern, ADR-002 §5), main-DB `F()` stat counters (visitors/impressions/conversions), client-side exit-intent/time triggers + localStorage frequency cap + page exclusions, overlay theme registry; `SocialProofSettings` `[S]` singleton (default OFF, `display_mode=anonymous` default), server-embedded 60 s-cached JSON feed (no public endpoint), PAID orders only, coarse time buckets, never-fabricate rule — **D6 ships anonymous-only at launch, named modes (`first_name`/`first_name_city`) gated behind the human/legal sign-off track (same track as ADR-025's beacon item); D3 ships single opt-in + consent proof for v1 with double opt-in enabled by default for DE-targeting stores; D4 localStorage frequency-cap/exclusion exemption reading accepted** — mandatory Safety Agent gate before release for both tickets — see `../../docs/adr/ADR-027-conversion-overlays.md` | ACCEPTED (human, 2026-07-11) |
| ADR-028 | A/B multi-variant product pages (`ProductPageVersion`, TICKET-042) — resolves AF-12's "page A/B test data model" PENDING; new `ProductPageVersion`/`ProductPageVersionImage` models (zero translatable fields, "variant" banned from all new identifiers to avoid collision with SKU `ProductVariant`), suffix-on-slug URLs (`<product-slug>-<suffix>`) registered per language in the Permalink table, canonical→primary + zero hreflang + sitemap-excluded (DECIDED FM-C4), link-level traffic split only (no automatic assignment, no consent-cookie surface), `page_version_id` soft-ID stamp carried in `Event.properties`/`CartItem`/`OrderItem` (ADR-004 amendment — no new `EventType`), deactivate-version = 302→primary (reversible) / delete-version = 301→primary (permanent, Pinterest link equity) — **all 5 PENDING product defaults (P1 cap 10, P2 all-languages automatic permalinks, P3 ≥1 image required to activate, P4 no significance testing in v1, P5 302/301 redirect semantics) approved as recommended** — mandatory SEO Agent gate (canonical/og divergence, sitemap, redirects) and Safety Agent gate (beacon/ATC-form `page_version_id` input validation, admin CRUD) before release — see `../../docs/adr/ADR-028-ab-variant-pages.md` | ACCEPTED (human, 2026-07-11) |
| ADR-029 | Automated gift-card campaigns (`GiftCardCampaign`, TICKET-045) — resolves the 11:#3 remainder's CRITICAL mystery "what auto-issued the 765 uniform $5 cards" (answer: the legacy platform's own automated gift-card campaign; this feature is its replacement); dedicated `discounts.GiftCardCampaign` model (not a new `campaigns.CampaignType` — no funnel/session/storefront UI needed), `order_paid`-only trigger vocabulary in v1 (`nth_order`/`spend_threshold`/`win_back` reserved), **fixed-value only** in v1 (`percent_of_order`/`percent_discount_coupon` stay reserved/PENDING), post-PAID-commit Celery task made idempotent by `CampaignReward` (ADR-002 §5 pattern) plus a campaign-row `SELECT FOR UPDATE` serializing cap checks (§XIII), **email-only** delivery in v1, new nullable `DiscountCode.gift_card_campaign` FK for the Issued/Used/Outstanding list counters — **5-year French statutory floor on expiry with configurable validity enforced by the campaign form based on the store's markets; refund policy = deactivate unspent code / flag spent code for manual review; self-gifting brake = zero-processor-charge-amount skip + per-recipient-email frequency cap — all approved as recommended** — mandatory Safety Agent gate (money-adjacent: webhook-driven issuance, code entropy, refund/void flow) before release — see `../../docs/adr/ADR-029-gift-card-campaigns.md` | ACCEPTED (human, 2026-07-11) |
| ADR-030 | Security badge designer (`badges.SecurityBadge`, TICKET-046) — **deliberate, human-approved product-integrity decision to deviate from the CommerceHQ reference screenshots**: no third-party certification marks ("McAfee Secured"/"Norton Secured"/TRUSTe/BBB/"24-Hour Surveillance"/"AES-256 BIT") ship as built-in assets, because the platform cannot verify any store's actual enrollment in such a service and shipping the mark anyway would be a false claim of certification; built-in badge presets registry (`badges/badge_presets.py`, same code-registry shape as ADR-027 D10's `OVERLAY_THEMES`) composes only self-verifying icons (lock/SSL seal gated on `request.is_secure()`) and payment-network logos **derived from the store's actually-enabled `PaymentMethod.method_family` rows** (never a fixed logo list); stores may still upload their own image to display a genuine third-party certification they actually hold; new `SecurityBadgeSlotProvider` registered into the already-existing, already-rendered ADR-012 `slot.security_badge` slot (product/checkout templates already call it; one-line addition to `cart.html`); all three locations (product/cart/checkout) ship in v1 — **live re-render preview pane stays out-of-scope, PENDING follow-up ticket** — see `../../docs/adr/ADR-030-security-badges.md` | ACCEPTED (human, 2026-07-11) |
| ADR-032 | Admin unique validation for store-excluded forms (TICKET-053) — the "store excluded from the form, set in save_model" convention kills friendly unique validation through **two independent Django 6.0.4 mechanisms** (traced: `_get_validation_exclusions()` drops any store-inclusive `unique_together`/`UniqueConstraint` check tuple-wise, AND `store_id=None` at validation time triggers the None-value skip in `_perform_unique_checks`/`UniqueConstraint.validate` even without the exclusion — `save_model` runs at options.py:1853, after `form.is_valid()` at :1847), so a same-store duplicate through any store-site admin's real HTTP endpoint 500s with a raw IntegrityError (pre-existing, repo-wide; flagged by TICKET-052 verification, distinct from and complementary to ADR-031 Addendum 3's proven model-layer fix); fixed by `StoreUniqueValidationFormMixin` (inject `instance.store` before `_post_clean` + `exclude.discard("store")` — both primitives load-bearing), wired **by construction** via `StoreScopedFormFieldsMixin.get_form`/`get_formset` and `StoreSafePKFormSetMixin._construct_form` parent-store stamping for inlines (zero per-admin edits; `save_model` conventions untouched); super site gets a structural rule instead of injection (add-capable admins over constrained StoreOwnedModels must expose `store` as a form field — the `DiscountCodeSuperAdmin` pattern, drift-test-enforced); global IntegrityError catcher rejected (lossy, absorbs unrelated defects into invisible failures — §XV-1; residual TOCTOU race = stock-Django posture, accepted); folds in the CsvListWidget 3-part display/**corruption** fix (`prepare_value` list passthrough — a no-op save of empty tags currently writes `tags=['[]']`; `to_python` empty→`[]`; `blank=True` + migrations on `Product.tags`/`Organization.coverage_areas_json`); drift test decided as structural-semantic sweep over both sites + 2 HTTP canaries (full-HTTP-per-admin rejected as a ~30-model fixture tax) — see `../../docs/adr/ADR-032-admin-unique-validation.md` | ACCEPTED (Architect adjudication, 2026-07-11 — no product-level choice involved: 500 → form error) |
| ADR-033 | Employee permission matrix (TICKET-047 + new TICKET-054) — frozen module vocabulary as a code registry (`stores/modules.py`: shipped keys canonical, AF-002 labels display-only — `pages`="CMS", `analytics`="Reports"; sole real drift `collections` merged into `products` via reversible max-rank data migration; `employees` as a separate 19th grid row — DECIDED, human 2026-07-11, per AF-002's Settings-escalation edge case); storage stays TICKET-002's `permissions_json` JSON matrix (per-module rows rejected: form ergonomics, 1-query-per-request resolver cache vs ~30 `has_module_permission` calls on the admin index, simpler drift-test story) with unknown-key/illegal-level rejection at `clean()` (§XV-5); enforcement **structural at registration** (TICKET-052 auto-wrap precedent — per-admin gating already drifted to 12 ungated store-site files): ONE resolver `stores/permissions.py::check_module_access` (§XV-4) replacing the seven per-app `_check_module_access` copies, `require_module` decorator for custom admin views, `StoreModulePermissionMixin` auto-mixed by `StoreAdminSite.register()` (view=`limited` threshold so Orders view-only falls out generically, mutate=`full`; declared `module_key` str/tuple/`MODULE_EXEMPT`; unmapped = fail-closed for limited employees + introspective drift-test failure — never silently open, never a registration crash; super site never matrix-gated); AF-002 grid UX as dynamic ChoiceFields composed into `permissions_json` (Full-Access mode hides but never wipes the grid), email-based invite via `send_transactional_email` (AF-108) with set-password token and stamp-once `accepted_at` (chat GDPR-stamp precedent), invited name/phone stored on `StoreEmployee` not `User` (no cross-store write surface); guardrails: no self-edit/self-escalation on the store site + last-active-full-access lockout prevention under `select_for_update` (§XIII, super site allows-with-warning); Django auth composition traced (6.0.4 sites.py/options.py): `is_staff`/`is_superuser`/`Permission`/`Group` play no role on either site — AF-C1 DECIDED (human, 2026-07-11): per-store matrix; Orders "Limited access" DECIDED: view-only — mandatory Safety Agent gate (permission system) — see `../../docs/adr/ADR-033-permission-matrix.md` | ACCEPTED (human, 2026-07-11 — full bundle approved: 19-module vocabulary with separate `employees` row, coupons→gift_cards mapping, structural enforcement, lockout guard, no self-edit; all D7 PENDING items DECIDED per recommended defaults) |
| ADR-034 | Per-store custom email sender domains (`SenderDomain`, TICKET-040) — extends `DECIDED Settings-C1` v1 (shared platform sender); new **plain-`Model`** `SenderDomain` (store FK, globally-unique `domain` — deliberately **not** `StoreOwnedModel`, same shape as `stores.StoreDomain`/ADR-008, since a beat task must scan `PENDING` rows across every store and two stores can never share a domain), explicit `UNVERIFIED→PENDING→VERIFIED|FAILED` state machine (§XV-3, never inferred from absence) with weekly re-check demotion, platform-generated 2048-bit RSA DKIM keypair (private key via the already-approved `payments/fields.py::EncryptedCharField`, no new crypto dependency) + rendered SPF/DKIM/DMARC-guidance DNS records, single new `emails/sender.py::resolve_from_email(store)` replacing both `DEFAULT_FROM_EMAIL` call sites in `emails/service.py` (§XV-4 — verified→custom From, else unchanged platform fallback, AC-183 regression preserved), **honest v1 scope**: ships "verified custom From-address + SPF pass + platform-DKIM alignment" only — full custom-domain DKIM signing needs the outbound relay configured with the store's key, documented as a release-checklist ops step, not Django code — **`dnspython` dependency DECIDED/APPROVED (human, 2026-07-11)** for the automated DNS-check beat task (the manual super-admin-only fallback is no longer needed; re-verification cadence for already-VERIFIED domains, ADR-034:P2, remains PENDING) — mandatory Safety Agent gate (domain verification = spoofing surface; manual override must be super-admin-only + audit-logged) — see `../../docs/adr/ADR-034-sender-domains.md` | ACCEPTED (human, 2026-07-11 — `dnspython` dependency approved; ADR-034:P2 re-check cadence still PENDING, non-blocking) |
| ADR-035 | TimescaleDB analytics migration (TICKET-044, revises ADR-004) — recommendation is **defer the hypertable conversion, ship the vendor-detection guards now**: at zero production traffic there is nothing yet to prove a chunk-management policy against, and ADR-004 already made the schema byte-identical on both backends for exactly this reason; a vendor+extension-aware `RunPython` migration (`pg_extension` check, logged not silent — §XV-1) gates `create_hypertable(if_not_exists=>TRUE, migrate_data=>TRUE)` as a no-op everywhere except a Timescale-enabled Postgres; `purge_events` gets the same branch (hypertable present → `drop_chunks`, absent → today's unchanged DELETE), both paths logging which one ran so an operator never has to guess; `aggregate_metrics` and its idempotent delete-then-insert `AggregatedMetric` writes are untouched and stay authoritative; continuous aggregates explicitly deferred as a documented **later** optimization, not v1; extension install stays an ops/release-checklist item, never an automatic side effect of `migrate` — see `../../docs/adr/ADR-035-timescaledb-migration.md` | ACCEPTED (human, 2026-07-11 — already decisive at proposal, no PENDING items) |

---

## ADR-001: Multi-tenant store architecture

**Status:** ACCEPTED (incorporates decisions 11:#8, feature matrix area 28).

### Context

Pradize is a single Django deployment hosting many stores. A store is one storefront site: one primary language (plus `/fr`, `/de` path languages — model PENDING 11:#9), one domain or subdomain, one theme, one catalog. Above stores sits a cross-store layer: Organizations (legal entities that receive payments), the payment control plane, shipping zones/carriers, automated email templates, themes, and up-sell campaigns shared across stores. The screen inventory confirms two admin surfaces (per-store `/admin/`, cross-org `/superadmin/`).

The dominant risk in every multi-tenant Django codebase is **data bleed**: a queryset in a view, report, or Celery task that forgets to filter by store and returns another tenant's orders. Per §XV-1, invisible failures are the enemy — a missing filter returns HTTP 200 with wrong data.

### Decision

**1. Three core entities:**

- **`Store`** — one storefront: `name`, `subdomain`, `custom_domain`, `primary_language`, `timezone`, `default_currency`, `theme` FK, `is_active`, soft-delete fields (`deleted_at` — delete semantics PENDING, area 26). One site, one primary language, one domain/path set.
- **`Organization`** — cross-store **legal entity** owning payment routing: legal/display name, registration country, settlement currencies, buyer coverage areas, statement descriptor, monthly volume threshold, `is_default` (exactly one default org enforced at DB level via partial unique constraint), status Active/Draft. Organizations are platform-global; any store's order may route to any eligible organization (see ADR-006). Store↔Organization relationship: see §3b below (DECIDED 2026-07-02).
- **`User`** — single custom Django user model shared by both admin sites: `email` (login), `is_store_admin`, `is_super_admin`, `last_login`. No per-store user tables.

**2. Two Django admin sites, one auth backend.**
`/admin/` is a per-store `AdminSite` instance; `/superadmin/` is a cross-org `AdminSite`. Both authenticate against the same `User` table. `is_super_admin` gates `/superadmin/`; store access at `/admin/` is gated by `StoreEmployee` rows, not by a global flag alone.

**3. `StoreEmployee` = User × Store with permission matrix.**
A user can be admin of multiple stores: one `StoreEmployee(user, store)` row per membership, with `permissions_json` holding the 18-module Full/No-access matrix (Orders "Limited access" semantics PENDING — 11 Part D), `full_access` master flag (interaction with the grid PENDING), `invited_at`, `is_active`, `last_login_at` (displayed in the employee list). Unique constraint `(user, store)`. Whether the matrix is per-store or org-wide is PENDING (AF-C1); the per-store row structure supports both — org-wide is simply "same matrix copied to every row".

**3b. Store↔Organization relationship — Organization is payment-only + loose store grouping (DECIDED 2026-07-02).**

- **Organization owns exactly two resource families: `ProcessorAccount`s and payment `RoutingRule`s** (`OrganizationRule`, pools, split counters — see ADR-006). Nothing else hangs off Organization.
- Stores may be **loosely grouped** under an Organization (nullable `Store.organization` FK) purely as a grouping/filter dimension (reports, super-admin filters, "store groups" in part E). This grouping carries **no config inheritance and no data-sharing semantics** — a store's orders can still route to *any* eligible Organization per ADR-006; the grouping FK plays no role in routing.
- **Platform-level super-admin (NOT the Organization model) owns the shared cross-store resources:** Themes, Shipping zones/carriers, and Campaign templates (automated email masters, up-sell campaign templates). These are platform-global rows managed at `/superadmin/`, visible to stores per the hybrid sharing semantics of §5 — they are never attached to an Organization.
- Consequence for the schema: no `organization` FK on Theme, ShippingZone, Carrier, EmailTemplateMaster, or Campaign templates; adding one is a design error.

**4. Store isolation enforced at the ORM layer — `store` FK on every store-scoped model, no exceptions.**

- Abstract base `StoreOwnedModel` with `store = ForeignKey(Store, on_delete=PROTECT)` and `objects = StoreScopedManager()`.
- `StoreScopedManager.get_queryset()` **raises** unless the query is explicitly scoped: the only public entry points are `.for_store(store)` and `.cross_store_unsafe()` (name is deliberately ugly; used only in super-admin reports and migrations). This makes the unscoped query the loud failure, not the silent one (§XV-1).
  - **AMENDED 2026-07-11 (ADR-031):** one carve-out — M2M **relation-bound** managers (Django builds them from the target's `_default_manager` class, so they inherit the raise and broke `model_to_dict`/`save_m2m` on every M2M-to-StoreOwnedModel field) get a real queryset instead of the raise; in exchange, a core `m2m_changed` pre_add guard raises `IsolationError` on any cross-store M2M row, so relation-bound reads stay store-safe by construction. Direct `Model.objects.*` and reverse-FK access still raise. Full reasoning: `docs/adr/ADR-031-relation-bound-m2m-reads.md`.
  - **AMENDED 2026-07-11 (ADR-031 addendum):** two sibling holes closed. (1) Admin formset hidden-pk lookups use the raw `_default_manager` (`BaseModelFormSet.add_fields`), undetectable at the manager level → fixed by `core/formsets.py` `StoreSafePKFormSetMixin` (pk queryset swapped for the formset's own parent-/admin-scoped queryset), enforced by introspective drift tests over both admin sites. (2) `_RaisingQuerySet` only raised on iteration/len/bool/fetch — `aggregate/count/exists/contains/iterator/aiterator/update/bulk_update/delete/explain` executed silently (unscoped `update()` = silent cross-store write) → all now raise; `Model.objects.none()` becomes a sanctioned safe entry point. The "raises unless explicitly scoped" guarantee above is only fully true as of this closure.
  - **AMENDED 2026-07-11 (ADR-031 Addendum 3):** the closure surfaced the validation ring — Django's `_perform_unique_checks`/`_perform_date_checks`/`UniqueConstraint.validate` and the default `ModelChoiceIterator` all query through the raw `_default_manager` and had only ever worked via the silent-`exists()` hole. Fixed by (1) a contextvar validation window on `StoreOwnedModel.validate_unique`/`validate_constraints` that unlocks ONLY `exists()` for Django's own constraint lookups (stock semantics preserved — user `clean()` code stays loud), and (2) `core/admin.py` `StoreScopedFormFieldsMixin` scoping FK/M2M widget querysets by construction, enforced by an introspective drift test over both admin sites. Details: `docs/adr/ADR-031-relation-bound-m2m-reads.md` Addendum 3.
- Storefront requests: middleware resolves `request.store` from `Host` header (custom domain / subdomain) once per request; views use `Model.objects.for_store(request.store)`.
- `/admin/` requests: middleware resolves the active store from the employee's memberships (store switcher when >1) and each `ModelAdmin.get_queryset()` applies `.for_store()`.
- Composite indexes lead with `store_id` on all hot tables (`(store_id, created_at)`, `(store_id, status)` …).

**5. Sharing semantics are per-resource, never global-by-accident** (§VI: three databases, three sharing semantics — do not conflate). Each entity in `05_database_schema.md` is explicitly marked **store-scoped** (has `store` FK), **platform-global** (control plane, carriers, currencies, email template masters), or **hybrid** (super-admin campaigns visible read-only in stores, per part E footer / super-admin-11 annotation).

### Consequences

- Adding a store-scoped model without the base class fails code review by construction (checklist + a unit test that introspects all models for the `store` FK).
- Cross-store analytics for super-admin must use the explicit unsafe manager — auditable via grep.
- Multi-store employees get a store switcher; no re-login between stores.
- Organization stays a thin payments entity — no temptation to hang themes/shipping/campaigns off it; those live at platform level under super-admin.
- PENDING items carried: per-store vs org-wide permission matrix, delete-store retention, domain→language→country model (11:#9). ~~Store grouping under orgs~~ DECIDED (loose grouping, payment-only ownership — §3b).

---

## ADR-002: Discount engine — unified DiscountCode model

**Status:** ACCEPTED (incorporates DECIDED 11:#2, 11:#3, stacking-order and one-gift-card decisions). **Complies with design-pattern-ideas §XIII (coupon/gift-card timing attack surface).**

### Context

The reference platform has coupons, sellable gift cards, manually issued gift cards, and campaign-auto-issued gift cards — with a "Gift card code" field appearing inside the coupon editor. Decision 11:#2 unified these into one code system. §XIII documents the two production traps: (a) check-then-act coupon validation over-issues limited coupons under concurrent checkouts; (b) gift-card balance decrement outside the order-payment transaction loses money on partial failure.

### Decision

**1. One `DiscountCode` model (store-scoped), type-discriminated:**

| Field | Notes |
|---|---|
| `store` FK | ADR-001 hard rule |
| `code` | unique per store, case-insensitive index |
| `type` | `coupon_pct` / `coupon_fixed` / `coupon_freeship` / `coupon_freeprod` / `giftcard_manual` / `giftcard_auto` (sellable gift cards produce `giftcard_manual` codes on purchase) |
| `value` | percent or amount depending on type; for gift cards = initial face value |
| `currency` | required for fixed/gift-card types. **PENDING:** currency across sub-stores and the "% discount as gift-card value" mode (11:#3 remainder — conflicts with stored-value model, kept out of v1 types) |
| `free_product` FK | only for `coupon_freeprod` |
| `valid_from` / `expires_at` | expiry-per-jurisdiction rules PENDING (11:#3) |
| `max_uses` / `uses_remaining` | `uses_remaining` is the atomically decremented counter |
| `per_email_limit` | per-customer limit keyed on email (guest checkout decision) |
| `stackable` | coupon stacking flag from admin-019 |
| `rules_json` | applicability constraints: product ids, collection ids, min order total (rule vocabulary PENDING — 11 Part C) |
| `is_active`, `campaign` FK nullable | link to the auto-issuing campaign for `giftcard_auto` |
| `provenance` | `merchant` / `lead_capture` / `campaign` / `sold` — answers the "765 × $5 cards" audit question |

**2. `GiftCardTransaction` — append-only stored-value ledger:**
`(discount_code FK, order FK, amount_used, balance_after, created_at)`. Current balance = `balance_after` of latest row (denormalized `current_balance` on `DiscountCode` maintained in the same transaction). Mixed payment refund order (DECIDED 2026-07-02, AC-U3/UF-K): **card refunded first** (up to the card-charged amount), remainder restores the gift card balance via a negative `GiftCardTransaction` reversal entry. The ledger design absorbs this as a new row type with a negative `amount_used`.

**3. Application order — coupon first, then gift card on remainder (DECIDED):**
Coupon (% or fixed) applies to the full original order total; the gift card covers the remaining balance; any residue goes to the payment processor. **One gift card per order** (DECIDED UF-H). One coupon per order unless `stackable`.

**4. Concurrency — the only safe patterns, per §XIII:**

- Coupon consumption:
  ```sql
  UPDATE discount_code
     SET uses_remaining = uses_remaining - 1
   WHERE id = %s AND uses_remaining > 0
     AND is_active AND expires_at > NOW();
  ```
  proceed **only if** `rows_affected == 1`. Never ORM check-then-save.
- Gift card redemption: `SELECT ... FOR UPDATE` on the `DiscountCode` row inside the **same DB transaction** that writes the order payment record and the `GiftCardTransaction` row. If the order write fails, the balance decrement rolls back — no lost balance.
- Per-email limit uses the same atomic idiom against a `(discount_code, email, use_count)` counter row inserted with `ON CONFLICT ... DO UPDATE ... WHERE use_count < per_email_limit`.
- Consumption happens at **payment authorization**, not at cart-apply; a released authorization restores the counter in the same webhook transaction.

**5. Auto-issued gift cards are idempotent** (§XIII): campaign triggers write a `CampaignReward(recipient_email, campaign_id)` row with a unique constraint **before** creating the code. A retried Celery task hits the constraint and issues nothing — the customer gets the $10 exactly once. **Field-name correction (dated 2026-07-11):** the shipped model's second key column is `campaign_id` — an **opaque `CharField`, not an FK** to `campaigns.Campaign` — so any caller (marketing campaigns, ADR-027's lead-capture overlays, future consumers) can mint its own stable string ref (e.g. `f"lead_capture:{pk}"`) without a schema change. Do not confuse this generic, FK-free `discounts.CampaignReward` with the funnel-specific `campaigns.CampaignIssuedCode` (ADR-009/ADR-010), which *does* carry real FKs to `CampaignSession`/`CampaignStep`/`AbandonedCheckoutEmailStep` — that model is for T027/T028 funnel/abandonment attribution only and is not a substitute for this one. See `05_database_schema.md` §3 for the corrected field list.

**6. Validation gates at persistence boundaries** (§XV-5): applicability (`rules_json`, dates, `is_active`) is validated at cart-apply for UX, and **re-validated inside the checkout transaction** — the cart-apply check is advisory only.

### Consequences

- Coupon usage stats ("used count", "Totalling") read the counter and the order-discount lines — definition of "Totalling" PENDING (11).
- The single model explains the coupon-editor "Gift card code" field and gives one admin list with type filters (admin-020 saved views).
- Open PENDING: gift-card value modes beyond fixed value, jurisdictional expiry, timer-promo × coupon precedence (UF-A). ~~UF-K refund policy DECIDED~~ (card first, remainder to gift card — 2026-07-02, AC-U3/UF-K).

---

## ADR-003: AI Job System

**Status:** ACCEPTED. **Complies with design-pattern-ideas §I (AI output validation), §IV (async state machines), §VII (stable IDs), §VIII (multi-phase generation), §XV-3/-6 (explicit state, idempotent jobs).**

### Context

The AI job system is the cross-cutting foundation (feature matrix area 24) for translation (always highest priority), review generation, timer-promo management, social media, email campaigns, pricing experiments, and statistics-triggered improvement jobs. §I and §IV document the production failure modes: silently truncated AI output persisted as valid, atomic-job retries that discard completed sub-steps, and dead workers leaving jobs `IN_PROGRESS` forever.

### Decision

**1. Entities** (store-scoped where the target content is store-scoped; see schema doc):

- **`AiJob`** — the unit of intent: `store`, `job_type`, `target_content_type`/`target_object_id` (generic FK), `priority` (translations highest), `status`, `requires_human_review`, `payload_json`, `created_by` (rule/trigger/human), `chunk_index`, `total_chunks`, `last_chunk_received_at`.
- **`AiJobRun`** — one execution attempt: `job` FK, `runner` (which terminal/API), `status`, `started_at`, `last_heartbeat`, `finished_at`, `error_msg`, `retry_count`, `tokens_in/out`, `cost_estimate`. A job has many runs; the job's status derives from its latest run plus validation.
- **`StoreAiQuota`** — monthly token/credit quota per store: `(store FK, month date, quota_tokens int, used_tokens int)`. Tracks monthly AI credit usage per store. Set by super-admin.
- **`AiJobDependency`** — `(job, depends_on_job)` DAG edges: phase-2 jobs (e.g. social-image generation after text generation, §VIII) are **separate jobs** depending on phase-1 jobs, each independently retryable. Never one mega-job.
- **`AiJobOutput`** — parsed output keyed by **stable field ID**: `(job_run, field_id, value, chunk_index)`. Field IDs are opaque object PKs + field name (`item_42_label`), never positions or display values (§VII). On duplicate field ID within a run: **append**, never overwrite (§I continuation rule).
- **`AiJobValidation`** — validation verdict per run: `(job_run, check_name, passed, details_json)`. All checks recorded, pass or fail — the dashboard shows *why* something failed (§XV-1).
- **`AiJobMetric`** — longitudinal quality measurement: `(job_type, store, metric_name, value, period)` — feeds the "AI verifies jobs happened well; measurements decide next job" loop.

**2. Status enum — explicit, no state inferred from absence (§XV-3):**
`NOT_STARTED / IN_PROGRESS / DONE / ERROR / SKIPPED` on both `AiJob` and `AiJobRun`. `ERROR` records are retried by the scheduler; partial content is **never** stored under `DONE`.

**3. Heartbeat pattern (§IV):** runners update `last_heartbeat` every N seconds while working. Scheduler reclaims any `IN_PROGRESS` run whose heartbeat is older than the dead-job threshold (default 3× heartbeat interval), marks it `ERROR(reason=dead)`, and re-queues respecting `retry_count`.

**4. Idempotent job design (§XV-6):** every step begins with "is step N already done?" — output rows keyed by `(target, field_id, lang)` short-circuit the AI call if the result already exists. Retrying a 5-step job with steps 1–2 done resumes at step 3. Sub-job types (`full`, `slug_only`, `phase2_image_only`, `chunk_N_of_M`) make partial retry addressable (§IV).

**5. Chunking is proactive (§I-6):** large inputs are split **before** hitting token limits (`chunk_index`, `total_chunks`, `last_chunk_received_at`). The continuation mechanism is treated as a failure signal, not a feature.

**6. Output validation BEFORE persisting (§I, §XV-5)** — persistence gate, all checks required:
- concatenate all assistant blocks in emit order; strip continuation suffixes (`re.sub(r'(?:_continued)?_part\d+$|_continued\d*$', '', field_id)`) before field-ID lookup;
- bracket/token balance checks (custom markers, HTML tags);
- minimum length ratio: output ≥ 35% of source length once source > ~200 chars (CJK-safe);
- API path: **assert** `stop_reason == "end_turn"`; treat `max_tokens` as ERROR, never as success;
- multi-field responses: validate ALL fields before committing ANY (§I) — no partial saves.
Failed validation ⇒ run status `ERROR`, nothing persisted, retry later.

**7. Runner selection — registry/plugin pattern:** each job type registers a class declaring `required_runner_capabilities` (image_gen, video_gen, command_exec …), `validate_output()`, `estimate_tokens()`, `build_prompt()`, `persist_output()`. Adding a job type = adding one registry entry (feature matrix: "pluggable job types, easy add/remove"). Runners: **terminal runners preferred** — Claude Code (default), Codex (image jobs), Gemini terminal (image/video) — polling the server via a Python script; **API calls are the last-resort fallback** when credits run low. Each store has a monthly AI credit quota (set by super-admin). Terminal AI runners (Claude Code, Codex, Gemini Terminal) are preferred and consume from the quota. When credits run low, the system falls back to direct API calls. A `StoreAiQuota` model tracks `(store, month, quota_tokens, used_tokens)`. (Decided 2026-07-02, FM-C2.)

**8. Human-review gate:** `requires_human_review=True` routes a validated-DONE job into a review queue (accept / reject / request-revision) before `persist_output()` publishes. Translations default to auto-publish; reviews/promos/emails default to human review (per feature matrix area 24).

### Consequences

- Every failure is visible in a dashboard by (job_type × status × store); zero silent partial persists.
- Cost: more tables and a scheduler loop — accepted, this is the backbone of the product's automation story.
- PENDING: AI-chat technology (11:#10), AI-review compliance flags (Part B). ~~FM-C2 memberships/credits DECIDED~~ (monthly token quota per store via `StoreAiQuota`, 2026-07-02).

---

## ADR-004: Analytics event tracking pipeline

**Status:** ACCEPTED — **REVISED 2026-07-02** (traffic estimate confirmed: **500+ orders/day per store — high volume**; storage moved from single main-DB events table to a **separate analytics database**). **Complies with design-pattern-ideas §X (beacon ordering, pixel dedup, first-touch UTM).**

### Context

First-party analytics power the dashboard, 12 report types, the owner's new columns (Collection display/CTR, scroll %, purchase rate), and the statistics-triggered job system ("collections to improve"). Data must be "realistic" and purged to avoid oversized DBs (extra-spec). §X documents the thank-you-page double-fire and checkout-referrer attribution traps.

**Traffic estimate (confirmed): 500+ orders/day per store.** At typical funnel ratios (~1–2% conversion) that is on the order of 25k–50k sessions/day/store and several hundred thousand raw events/day/store; a few dozen stores puts sustained ingest in the tens of millions of events/day. A single Postgres events table sharing the main Django database is not viable at this volume: analytics INSERT bursts would compete for WAL, I/O, and vacuum with transactional writes (orders, payments, gift-card ledgers) — exactly the writes that must never stall.

### Decision

**1. Separate analytics database — never the main Django DB.**
The `Event` table and its aggregates live in a **dedicated analytics database**. `FiredPixel` lives in the **main transactional Django DB** (see §5). The analytics DB has no FK dependencies on the main DB — this is preserved by keeping `FiredPixel` out of the analytics DB.
- **TimescaleDB strongly recommended** (`Event` as a hypertable, `timestamp` chunking, native compression on closed chunks, `drop_chunks` for the purge policy).
- **Minimum acceptable alternative:** a separate plain-Postgres database (separate server or at least separate instance/volume) with **monthly declarative partitioning** on `timestamp` and `(store_id, timestamp)` indexes per partition. Purge = detach/drop old partitions, never row-level DELETE.
- The analytics DB is **write-mostly** (beacon ingest) and **read-mostly for reporting**; it has no foreign keys into the main DB and the main DB has none into it. Separating it prevents analytics writes from competing with transactional writes (orders, payments) for I/O, locks, and autovacuum.
- Configured as a second Django `DATABASES` entry + DB router **for reads/admin only**; the ingest path bypasses the ORM entirely (see §3b).

**1b. One append-only `Event` table (store-scoped, time-series), in the analytics DB:**
`(event_type, session_id, store_id, product_id?, collection_id?, url, referrer, utm_source, utm_medium, utm_campaign, device_type, country, customer_local_hour, value, timestamp)`. Partitioned/chunked by time, indexed `(store_id, timestamp)`. **All references are soft IDs — no FK constraints at all** (neither to catalog rows nor to `Store`): events must survive catalog deletes, stay cheap to insert, and the analytics DB cannot hold cross-database FKs anyway.

**2. Event types (closed enum, v1):**
`page_view`, `product_impression`, `product_click`, `collection_impression`, `collection_scroll_depth`, `add_to_cart`, `checkout_start`, `purchase`. New columns' definitions:
- **Collection CTR** = `product_clicks ÷ product_impressions` per collection (impressions counted per product card actually rendered in viewport).
- **Scroll depth** = percentage of the collection page scrolled before the **first** product click (one `collection_scroll_depth` event per collection view, emitted at click time or page exit).
- Exact denominators for the remaining owner columns stay PENDING (11 Part A CRITICAL) — the event vocabulary above is designed to support every candidate definition, so collection can start before the definitions are locked.

**3. Collection JS:** the beacon is inlined into the page's existing script block (no separate async file — §X ordering problem); it batches events and posts to a lightweight ingest endpoint (no session middleware, no ORM save signals — raw insert).

**3b. Ingestion path — async, ORM-free (high-volume rule):**
- The ingest endpoint validates + enqueues; actual persistence is **asynchronous** — either a Celery task that batch-inserts, or a direct insert from the endpoint worker into the analytics DB. Either way, **no Django ORM on the ingest path**: batched **raw SQL (`COPY`/multi-row `INSERT`) or asyncpg** for performance. Model-instance creation per event is forbidden at this volume.
- Ingest failures must be visible (§XV-1): a dropped-batch counter and a dead-letter queue for malformed batches — never silent discard.
- Beacon loss is tolerated (analytics, not accounting); **money-relevant events (`purchase`) are additionally written server-side** at payment confirmation, not only from JS.

**3c. Reporting reads the analytics DB only.** All 13 report types, the dashboard, aggregation jobs, and the statistics-triggered job selectors query the analytics DB exclusively. Report generation never touches the main Django DB (store/product *names* for display are resolved from the main DB by ID in the thin presentation layer, not joined in report SQL).

**4. Attribution — first-touch (DECIDED direction from §X):** UTM parameters are captured at the **first page view of the session** and stored in the session record; at purchase they are copied onto the `Order` (`utm_source/medium/campaign`, `first_referrer`, `landing_page`). Checkout-time referrer is never used for attribution. Direct bucket = no UTM and no external referrer at first touch. (Attribution window / first-vs-last-touch formally PENDING in 11 — first-touch is the implemented default pending override.)

**5. Pixel dedup — `FiredPixel` table (§X):** `(order, pixel_type, event, fired_at)` with unique `(order, pixel_type, event)`. **Insert BEFORE firing** (insert → fire, not fire → insert) so a thank-you-page reload finds the row and skips the pixel. Purchase pixels fire on payment confirmation, not on page load alone. **`FiredPixel` lives in the MAIN transactional Django DB (main Postgres), not the analytics DB.** It carries an `order` FK and a correctness-critical unique constraint that must be enforced before the pixel fires — analytics DB availability must not gate this dedup check. The table is tiny (one row per order × pixel type). `FiredPixel` is managed via Django ORM, not the raw-SQL analytics ingest path.

**6. `customer_local_hour`** computed at ingest from delivery/geo country (multi-timezone countries approximated — PENDING refinement, 11 Part A), stored denormalized so the Sales-by-hour report is a plain GROUP BY.

**7. Purge policy:** raw `Event` rows purged after **N days (default 90, per-store configurable)**; nightly aggregation jobs roll them into `AggregatedMetric`-style summary rows (per store × day × dimension, in the analytics DB) retained indefinitely. Purge is implemented as chunk/partition drops (`drop_chunks` on TimescaleDB, partition detach on plain Postgres) — never row-level DELETE at this volume. Reports read aggregates for closed days and raw events only for today. Dashboards therefore stay fast and the DB bounded (extra-spec requirement).

**8. Money metrics (DECIDED AF-C5):** Gross Sales = sum of order totals at payment time, before refunds; Net Sales = Gross − Refunds; discounts and shipping are separate columns. The stray "Net sales" label on the state report is a bug to normalize.

### Consequences

- One table + one enum = one place to audit; adding an event type is additive.
- Aggregation jobs are part of the AI/automation job scheduler infrastructure (plain rule-triggered jobs, ADR-003) — they run against the analytics DB only.
- Operational cost: a second database to provision, back up, and monitor per environment (dev can run both DBs on one instance; prod must not).
- Cross-DB joins are impossible by design — report SQL that "just joins products" fails loudly instead of silently coupling the systems.
- PENDING: final column definitions/denominators, attribution window, multi-TZ bucketing, state-report country list.

### Risks

- **Two-database drift:** soft IDs mean a renamed/deleted product still appears in historical reports under its old ID — accepted; the presentation layer resolves current names and labels missing ones as "(deleted)". Never "fix" this with FKs.
- **Ingest queue backlog:** if Celery/batch workers fall behind at peak (flash sale), events arrive late — dashboards for "today" lag. Mitigation: batch-size + worker autoscaling metrics, dropped-batch counter dashboarded (§XV-1). Purchase events are immune (server-side write at payment confirmation).
- **Raw-SQL ingest bypasses ORM validation:** schema changes to `Event` must update the ingest writer and the beacon contract together — the event vocabulary is a versioned contract; a contract test asserts writer SQL columns == table columns.
- **TimescaleDB operational novelty:** if the team cannot operate TimescaleDB on day 1, start on the plain-Postgres-with-monthly-partitions minimum; the schema is identical, only chunk management differs (see Rollback/Migration).
- **Eventual consistency for reports:** "orders in Django admin" vs "purchases in analytics" can differ transiently (async ingest). Reports are analytics-DB truth; accounting is main-DB truth. A nightly mismatch detector compares purchase-event counts vs order counts per store×day and dashboards discrepancies (§XV-1).

### Rollback strategy

- **Rule edits/config:** analytics settings (purge N days, event enum additions) are live-on-save; rollback = revert the setting. Event-enum removals are never destructive — old rows keep their type.
- **Storage backend:** the schema is deliberately identical on TimescaleDB and plain partitioned Postgres. Rollback from TimescaleDB → plain Postgres (or the reverse migration, Phase-3 ticket) = logical dump/restore of raw events within the retention window + aggregates; no application-code change beyond the connection string and the chunk-maintenance job.
- **Catastrophic analytics-DB loss is survivable by design:** the main DB (orders, payments) is untouched; money reporting (Gross/Net Sales) can be rebuilt from orders. Raw behavioral events within retention are lost — accepted risk, documented to store owners. This asymmetry is exactly why the separation exists.
- **Ingest-path rollback:** if the async pipeline misbehaves, a feature flag switches the endpoint to synchronous direct insert (degraded but simple) while keeping the beacon contract unchanged.

---

## ADR-005: URL resolution and permalink model

**Status:** ACCEPTED. **Complies with design-pattern-ideas §II (NFD slugs), §III (single URL resolver), §XI (hreflang in base layer), §XII (redirect history).**

### Context

Every store serves multiple languages (primary domain + `/fr`, `/de` paths), products can have A/B variant URLs (canonicalized to primary — DECIDED FM-C4), and slug changes must never orphan indexed URLs. §III documents the wrong-language-links bug class; §XII the lost-link-equity bug class; §II the diacritic slug corruption. All three were production incidents in WebsiteEmpire2.

### Decision

**1. One slug function — single source of truth (§II):**
```
NFD-normalize(lower(name)) → strip combining marks (category Mn) → [^a-z0-9]+ → '-' → strip('-')
```
Used by **every** slug producer (products, collections, pages, gift-card pages, AI-generated translated slugs) **and by the redirect table's path normalization** — the exact same function, imported from one module. Django's `slugify` is wrapped, never called directly.

> **Open question (SEO Agent, 2026-07-03):** gift-card pages appear in the slug-producers list above, but no gift-card page type is defined in `07_multilingual_seo.md` URL/indexing tables. Before T016 freezes the reserved-slug list, decide: does the engine have a `/gift-cards/` landing page type (indexable public page), or is the gift-card screen admin-only (no public URL, no slug producer needed)?

**2. Translated slugs live in `PermalinkTranslation`, never on the content row (§III):**
`(store, content_type, object_id, lang, slug, status)` with unique `(store, lang, slug)`. Content models keep no per-language URL fields. `ProductTranslation` etc. hold translated *text*; the URL-owning slug row is `PermalinkTranslation`.

**3. `PermalinkResolver` — the only way to build an internal URL:**
Built once per request (and cached per store+deploy epoch): a map `(content_type, object_id, lang) → translated_url`. Every consumer calls it — templates (via one template tag), menus, breadcrumbs, canonical, **hreflang**, sitemaps, feeds, emails, API serializers. **No inline URL construction anywhere** (§III, §XV-4). Menu items store the source object reference, never a resolved href; resolution happens at render time for `request.LANGUAGE_CODE`.

**4. Hreflang from the same map (§XI):** generated in ONE base-template block (`{% block seo_head %}`) shared by every page type; legal pages override with a no-op. Hreflang set = live translations ∩ available-countries for the product (geo-restricted products only emit hreflang for sellable locales). Cached per page — never one query per tag.

**5. A/B variant URLs (DECIDED FM-C4):** variant URLs resolve through the same resolver, carry `rel=canonical` to the primary product URL, and are excluded from the sitemap.

**6. Redirect table — `PermalinkRedirect` (§XII):**
`(store, old_path, new_path, type: 301/302/none, auto_created, trigger: slug_change|domain_park, created_at)`.
- **Auto-creation via `pre_save` signal** on every slug-bearing model (works for admin edits, API updates, bulk imports, AI jobs — not view-level).
- Slug change on an entity published > 7 days requires an explicit confirmation step in admin (§XII).
- **`none` type** marks intentionally dead URLs so a reused slug never inherits an old redirect.
- **Chain collapsing at write time (§XII):** creating A→B when B→C exists writes A→C; creating B→C rewrites all existing X→B rows to X→C. Request time reads exactly one row. Loop creation is rejected at save.
- **No manual redirect creation in store admin (DECIDED):** the redirects screen is view/manage only (toggle, edit destination, bulk delete). Auto-created rows are safe to bulk-delete; the (super-admin-only, if ever) manual rows warn first.

### Consequences

- A single resolver makes wrong-language links structurally impossible rather than review-dependent.
- Cost: resolver cache invalidation on slug publish — tied to the same signal that writes redirects, so one code path.
- PENDING: domain→language→country model (11:#9) shapes the resolver's URL prefixing, not its interface.

---

## ADR-006: Payment routing — Control Plane architecture

**Status:** ACCEPTED (incorporates DECIDED 11:#4 live-on-save, 11:#7 Stripe+PayPal day 1). **Complies with design-pattern-ideas §IX (decision tree, running-counter split, decision log).**

### Context

Super-admin part E specifies two-stage routing: Stage 1 geography → one Organization or an OrgPool; Stage 2 per-pool allocation (% income split or threshold switch) → default-org fallback. §IX warns that a flat priority list breaks on unhealthy processors, transaction limits, and per-order random splits; the real structure is a decision tree with explicit fallbacks.

### Decision

**1. Decision tree, not a flat list.** **`OrganizationRule`** (platform-global): `(stage: stage1_geography | stage2_allocation, type: geography | allocation, conditions_json, outcome_json, org_pool FK nullable, fallback_rule FK self-referential, priority_within_stage, is_active)`.
- **Geography rule** (stage 1): conditions = countries/areas (EU, ROW…), currency, amount bounds; outcome = **one Organization OR one OrgPool**. First match wins within stage-1 ordering; the broad-rule-shadows-narrow paradox (Part E) is mitigated by a save-time lint that warns when a broader geography precedes a narrower one.
- **Allocation rule** (stage 2, bound to an OrgPool): `type = percent_split | threshold_switch`. Threshold switch consults each org's current-month volume vs its cap.
- **Explicit fallback:** `fallback_rule` FK makes the chain auditable (§IX). Chain exhausted ⇒ **default Organization's default processor**. When all routing options are exhausted (all orgs at cap), the system falls through to the default organization (which has no monthly cap). The DecisionLog records `reason='all_orgs_at_cap'`. (Decided 2026-07-02, AF-C6. Never silently reject a paying customer.)

**2. Percent split via running counter, never per-order random (§IX):** **`ProcessorSplitCounter`** `(org_pool, organization/processor_account, period, count, target_ratio)`. Routing reads the counters, picks whichever member is furthest **behind** its target ratio, then increments in the same transaction. Exact ratios over time, correct even at low volume.

**3. Health check at evaluation time, not configuration time (§IX):** `ProcessorAccount.is_healthy()` (health-status machine + manual block flag) is consulted during evaluation; a rule pointing at an unhealthy processor **falls through to its fallback automatically** — no rule reconfiguration needed. Health source (probes vs manual) PENDING (Part E).

**4. `DecisionLog` — written on every routing decision, no sampling:**
`(order_id, evaluated_rules_json, chosen_processor, chosen_organization, reason, over_cap, timestamp)`. This is the single most important debugging table (§IX): it answers "why did this order go to processor B" without reproduction. Retention/PII policy PENDING (Part E) — design: store rule IDs and outcomes, not buyer PII.

**5. Live-on-save (DECIDED 11:#4):** rule edits take effect immediately; no staging/publish versioning. Rollback = edit or delete the rule. The DecisionLog provides the audit trail that versioning would otherwise give.

**6. Layering:** stage 1+2 choose the **Organization** (merchant of record). The chosen org's `ProcessorAccount`s for the buyer's payment-method family are then ordered by the method-level option list (`backup_chain` vs `alternate_evenly` — `alternate_evenly` also uses a `ProcessorSplitCounter`). The exact composition of org-level split with method-level strategy is PENDING (Part E layer-ordering CRITICAL); the two-counter design keeps both layers independently correct.

**7. Day-1 connectors: Stripe + PayPal only (DECIDED 11:#7).** All processors sit behind a connector registry (adapter interface: authorize, capture, refund, health-probe, delayed-capture capability flag); HiPay/Mollie/BTCPay/BitPay are later registry entries. Credentials are encrypted in the Django main DB using **django-fernet-fields** (FERNET_KEY env var, separate from SECRET_KEY, AES-256 at rest). Custom `__repr__` ensures keys never appear in logs. The `vault://` URI scheme in the super-admin SVG design resolves to this encrypted DB field. No external secrets service required for v1. (Decided 2026-07-02, Settings-C3.)

### Consequences

- Every routing decision is explainable post-hoc; merchant support tickets become log lookups.
- Save-time lints + simulation page (content PENDING) provide the safety that staging would have provided under live-on-save.
- PENDING carried: health source, org-threshold accounting (what counts, month boundary, currency conversion), layer composition. ~~Cap-cascade (AF-C6) DECIDED~~ (fall through to default org, `reason='all_orgs_at_cap'` in DecisionLog). ~~Vault backend DECIDED~~ (django-fernet-fields, Settings-C3).

---

## ADR-007: Upsell funnel state machine

**Status:** ACCEPTED — **Safety Agent review RESOLVED (2026-07-03): all 9 amendments applied; T028 cleared for implementation after T027.** (Per DECIDED 11:#5.) **Complies with design-pattern-ideas §XIV (funnel state machines) and §VIII (never infer state from absence).**

### Context

Post-purchase one-click upsell (DECIDED: capture-window model, multi-step funnel from day 1 — UF-J), plus before-checkout recommendations, order bumps, storewide-discount timers, and abandoned-checkout sequences. §XIV warns against implementing each as an isolated modal: no unified view, no exclusion control, no sequencing.

### Decision

**1. `CampaignSession` — one explicit state machine per customer × campaign:**
`(store, order FK nullable, cart FK nullable, customer_email, campaign FK, state, step_index, data_json, capture_window_expires_at, created_at, updated_at)` with unique `(order, campaign_type)` — **one session per (order, campaign_type)**.
- **States:** `NOT_STARTED / IMPRESSION / INTERACTION / CONVERTED / DISMISSED / EXPIRED`. Every transition is written; absence of a session means "not eligible / never evaluated", never "declined" (§VIII — absence of data means *we don't know*).
- `data_json` records what was actually shown (offer product, price, step) — the unified "what did this customer experience" view.

**2. Capture-window payment model (DECIDED 11:#5) — two-charge baseline:**

- Checkout **authorizes** the original order amount (no capture), **and calls `setup_future_usage=off_session`** (Stripe) or the equivalent vault mechanism (PayPal) to save the payment method for off-session reuse within the capture window.
- Thank-you page shows the step-0 upsell offer while `now < capture_window_expires_at` (default **10 minutes**, configurable per campaign).
- **Accept** → add upsell item to the existing order as `PENDING_CAPTURE` → capture the original auth at the original amount → **charge the upsell as a separate off-session payment** (new PaymentIntent, `off_session: true`, `confirm: true`) using the vaulted payment method. **Upsell is charged immediately on accept via the stored payment method (consistent with AC-141).** Each charge produces an `OrderCharge` row; refunds target `OrderCharge` rows, not the order directly. A failed upsell charge must never endanger the original order: capture original first; upsell charge failure ⇒ upsell item removed/marked failed, shopper sees a soft failure, original order unaffected.
- **Single combined capture** (original + upsell in one processor call) is permitted **ONLY when** (a) the processor account has overcapture or incremental-authorization enabled **AND** (b) the upsell amount is within the processor's confirmed headroom for this specific authorization. This is a capability-flagged optimization, **never the default**.
- **Decline all steps or window expiry** → capture original amount only; void/remove all `PENDING_CAPTURE` upsell items; session → `DISMISSED` / `EXPIRED`.
- A watchdog job captures any still-authorized order at window expiry — an authorization is never left to lapse (money-losing invisible failure, §XV-1).
- The upsell UI never renders after expiry — eligibility check reads `capture_window_expires_at`, not UI state.
- Requires connector `supports_delayed_capture` (Stripe: yes; PayPal: to confirm in connector spec). Orders routed to a non-supporting processor simply get no post-purchase funnel — the eligibility check consults the DecisionLog's chosen processor.
- Pre-orders are **full charge at placement** (DECIDED FM-C3) — they are captured immediately and are therefore excluded from post-purchase capture-window funnels in v1.

**3. Accept/decline/watchdog race guard:**

All three transitions (accept, decline, watchdog expiry) use an atomic `UPDATE … WHERE campaign_session.status = 'AUTHORIZED' AND affected_rows == 1`. The first writer wins; losers abort without a processor call. Idempotency keys: `capture:{order_id}` for the original capture, `upsell:{order_id}:{step_id}` for each upsell step. Window check (accept path): evaluate `now < capture_window_expires_at` using DB time (`NOW()`) **inside the same locked transaction** as the state transition, before any processor API call.

**4. Processor account pinning:**

At authorization, persist the resolved `ProcessorAccount` FK on the `Order` (field: `processor_account`). All subsequent operations (original capture, upsell off-session charge, watchdog, refunds) use **only** that stored account. The routing engine (T020) is **never re-invoked** for any operation on an existing order. Upsell charges do **not** increment `ProcessorSplitCounter` — only the original authorized charge counts for split allocation. The upsell charge is logged as a child `DecisionLog` entry referencing the original decision, `reason=upsell_charge`.

**5. Thank-you page access control (security-critical, especially with guest checkout):**

Signed order-scoped tokens, two purposes:
- `thankyou-view` token: long-lived (lifetime = order review window, ~72 h), sent in confirmation email, grants GET access to the thank-you page.
- `upsell-act` token: short-lived (lifetime = capture window, 10 min), **single-use**, embedded in the thank-you page DOM, required for POST to accept/decline endpoints.

Accept/decline endpoints: POST only, CSRF-protected, rate-limited (3 attempts per order, then auto-decline). Opaque order IDs as defense in depth (do not expose sequential integer PKs in URLs). Without this, a guessable order URL becomes a charge-forgery primitive against a stranger's vaulted payment method. Token value is stored as a hash; raw value returned only on creation. Tokens must never appear in analytics events, pixels, or `DecisionLog`.

Acceptance test: AC-145 (see `08_acceptance_tests.md`).

**6. Provisional upsell item state and crash recovery:**

Upsell items are written to the order as `status=PENDING_CAPTURE`, invisible to fulfillment triggers, shipping triggers, and confirmation emails, until their payment charge is confirmed via webhook. Watchdog covers two cases:
- (a) `AUTHORIZED` past window → win the state transition, capture original amount only, void/remove all `PENDING_CAPTURE` items; session → `EXPIRED`.
- (b) Stuck `CAPTURE_IN_PROGRESS` → **reconcile with the processor first** (retrieve the PaymentIntent/authorization status) — the capture may have succeeded with the response lost; the idempotency key makes a re-issued capture safe. Proceed as (a) only if genuinely uncaptured.

Watchdog runs on a schedule tighter than the auth validity window with alerting on any order that stays uncaptured past window + grace (§XV-1 invisible-failure rule).

**7. Multi-charge refund model:**

An `OrderCharge` table records each capture/charge (original authorization capture, upsell off-session charge) with fields: `order` FK, `processor_charge_id` (str, unique), `amount` (decimal), `captured_at` (datetime), `refunded_amount` (decimal, default 0). One row per processor charge; original capture and each upsell step get their own row. Refunds target `OrderCharge` rows, with upsell charge refunded first, then original capture. Refund cap = Σ captured − Σ refunded across all charges. All refund calls use the stored `processor_account` FK.

**8. Multi-step funnel:** `Campaign` has ordered `CampaignStep` rows; each step defines the offer and **two branches**: `next_step_on_accept`, `next_step_on_decline` (stable step FKs, never positional indexes — §VII/§XV-2, so deleting step 2 cannot make step 3 inherit its slot). `CampaignSession.step_index` advances along the branch taken; terminal branch ⇒ `CONVERTED` (≥1 accept) or `DISMISSED` (0 accepts). All accepted steps are charged immediately via the vaulted payment method (two-charge baseline, §2 above).

**9. Campaign eligibility & exclusion:** evaluated once at funnel entry (checkout entry / thank-you load): priority-ordered active campaigns filtered by targeting rules (shared rule-builder), country, frequency caps, and **exclusion rules — campaign B does not fire if campaign A already has a `CONVERTED` (or configurable: any `IMPRESSION`) session for this order/cart**. Two-level ownership — precedence DECIDED (2026-07-04): **the store-admin campaign wins at runtime** when a product has both a store-admin and a super-admin campaign; the super-admin campaign applies only when no store campaign exists for the product. Super-admin campaigns are visible read-only in the store-admin view.

**10. Abandoned-checkout uses the same machinery:** each email step is a scheduled job with its own status (`abandoned → email_1_sent → email_2_scheduled → coupon_issued → converted`) recorded as CampaignSession transitions + per-step records — never a single enum on Order (§IV). Resume-link token lifetime = cart lifetime = **30 days** (DECIDED UF-E).

**11. Safety Agent gate:** the two-charge payment flow (auth + off-session upsell charge, partial-failure handling, webhook race with the watchdog) must pass Safety Agent review before implementation. **Safety review RESOLVED (2026-07-03) — all 9 amendments applied per the Safety Agent checklist. T028 is cleared for implementation after T027.** This ADR freezes the data model; the payment-flow edge cases were the review's scope.

### Consequences

- One queryable table answers "what funnels did this order see"; split-test reporting and frequency capping read the same rows.
- Capture-window model delays fund capture by up to the window for every funnel-eligible order — accepted trade-off, bounded by the watchdog.
- ~~PENDING carried: super-admin vs store campaign precedence, buy-X-get-X cap × stacking, storewide-discount next-order constraints, abandoned-checkout send-delay field placement (AF-C4).~~ **All four RESOLVED (2026-07-04):** store-admin campaign wins (super-admin read-only, applies only when no store campaign exists); buy-X-get-X has **no free-value cap** (stacking toggle retained); storewide-discount next order = **single-use `CampaignCode`** with configurable expiry ("Discount code expires after N days", default 30); send-delay field lives **in the email wizard** (`send_delay_hours`, required, default 1 h, "Send after" number + hours/days dropdown).

---

## Cross-cutting principles adopted (from design-pattern-ideas §XV)

1. **Invisible failures are the enemy** — every async operation has a dashboardable status; counters and mismatch detectors ship with the feature, not after the incident.
2. **Never use position as identity** — stable opaque PKs in AI protocols, campaign steps, menu items, collection ordering.
3. **State is explicit, not inferred from absence** — `NOT_STARTED/IN_PROGRESS/DONE/ERROR/SKIPPED` everywhere.
4. **Shared resources get a single resolution function** — PermalinkResolver, discount validation, payment routing evaluator, media lang-fallback helper.
5. **Validation gates at persistence boundaries** — AI output, coupons, slugs.
6. **Idempotent jobs, not idempotent steps** — every job checks "is step N already done?".
