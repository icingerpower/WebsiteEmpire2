# Release Checklist — Pixel Integrations + Currency Display + AI Sales Chat

**Date:** 2026-07-11
**Areas:** (1) pixel integrations (TICKET-030 / ADR-022); (2) currency display converter,
storefront + admin/super-admin (TICKET-031 + TICKET-037 / ADR-023); (3) AI sales-assistant
chat (TICKET-038 / ADR-024).
**Verdict:** READY FOR STAGING (all three areas). READY FOR PRODUCTION WITH CONDITIONS for
currency and chat. Pixels: READY FOR PRODUCTION WITH CONDITIONS for **non-EU** stores;
~~**BLOCKED FOR EU LAUNCH** pending the cross-backlog GDPR/ePrivacy consent feature.~~
**UPDATE (2026-07-11, Release Manager — consent-management release validated):
RESOLVED-by-ADR-025, CONDITIONAL.** The GDPR/ePrivacy consent feature
(TICKET-048/049, ADR-025) has shipped and been independently re-verified — see
`docs/releases/consent-management/` for the full checklist. Pixels are no longer a blanket
EU blocker; see the rewritten EU-launch position in `KNOWN_RISKS.md` item 1 and in the "Per-
area verdicts" section below for the exact, still-open conditions (deployment sequencing,
legal sign-off on the beacon exemption, deploy-checklist live checks, and a newly-discovered
caveat on the non-EU acknowledgment escape hatch, RM-1). Not a blanket READY — see per-area
verdicts at the end.

All findings below were independently re-verified by the Release Manager (commands run,
files read, greps executed) in this review cycle — none are taken on the reporting agents'
word alone.

---

## Checklist

### Spec approved (by human where critical)
PASS for all three areas, with open low-priority product decisions carried (not
implementation-blocking):
- **Pixels (ADR-022):** the D4 provider-mapping bundle (5 providers, GA4-only, no UA,
  client-side-only v1, `catalog_item_id` convention) is **DECIDED (human, 2026-07-10)** —
  verified struck-through in `specs/ecommerce_engine/11_uncertainties_to_validate.md` line 99.
  PIX:P1 (thank-you-token confidentiality vs pixel page_view URLs) is **DECIDED (human,
  2026-07-11)** — strip the token via a GA4-only `page_location` override — verified struck
  through at lines 100-115 and implemented in code (see Implementation section).
- **Currency (ADR-023):** display-only vs transactional, rate source (manual + optional ECB
  auto-refresh, off by default), `.99` rounding are **DECIDED (human, 2026-07-10)** — line 91.
  0-decimal-currency rounding convention **DECIDED (human, 2026-07-10)** — line 93. The
  "one global enable toggle" framing superseded by per-currency enablement, **DECIDED (human,
  2026-07-11 addendum)** — line 95. `show_code`/`code_placement` (admin-011 "Code visibility")
  restored after being dropped between the screenshot spec and the ADR's original draft —
  **DECIDED (human, 2026-07-11)**, ADR-023 §2 addendum, shipped in migration
  `currency/migrations/0003_currencydefinition_code_placement_and_more.py`. Remaining open:
  the exact 3 non-ECB currencies to reach "34 currencies" — **DECIDED (human, 2026-07-10)**:
  seed only the ~30 ECB currencies + EUR at launch, super-admin adds more later manually — not
  a blocker.
- **Chat (ADR-024):** AI chat technology choice (direct Claude API, read-only tools, SSE,
  Haiku default, GDPR opt-in off by default) is **DECIDED (human, 2026-07-10)** — line 18.
  The chat-on-checkout suppression and `chat` → `chat_launcher` slot rename are recorded as
  **human decisions (2026-07-11)** in the ADR-024 addendum and are verified **implemented**
  in code in this review (the addendum text says "in progress... not yet merged" at the time
  it was written; current code shows both landed — see Implementation section).

No PENDING-APPROVAL items remain that block implementation for any of the three tickets
(TICKET-030/031/037/038 all show "No PENDING blockers remain" in
`specs/ecommerce_engine/10_implementation_tickets.md` lines 482, 554, and TICKET-030's own
entry references the DECIDED bundle above).

### Architecture decisions recorded
PASS with one paperwork gap flagged, matching the pattern from the last release cycle:
- **ADR-024 (chat): ACCEPTED (2026-07-10)** — verified via
  `grep "^\*\*Status" docs/adr/ADR-024-ai-sales-chat.md`. Carries a substantive addendum
  (2026-07-11, Architect) ratifying 4 flagged assumptions (policy-page FK modeling, session
  TTL, `KILLED` enum, pricing table) and 2 human decisions (chat-on-checkout suppression,
  `chat_launcher` rename) — fully verified against code below.
- ~~**ADR-022 (pixels): still PROPOSED (2026-07-10).**~~ **UPDATE (2026-07-11, Release
  Manager, verified this cycle): `grep "^\*\*Status" docs/adr/ADR-022-pixel-integrations.md`
  → `Status: ACCEPTED (human, 2026-07-11; proposed 2026-07-10)`.** Two dated addenda exist
  and are both implemented and verified: the PIX:P1 addendum (2026-07-11, token-strip
  decision) and the risk-note correction for MEDIUM-2 (analytics-integrity **and**
  confidentiality, not analytics-only). Also carries the ADR-025 D0 wording correction
  ("slot-provider-only" → "pixels-app + claim-site change"). This gap is now closed.
- ~~**ADR-023 (currency): still PROPOSED (2026-07-10, Architect).**~~ **UPDATE (2026-07-11,
  Release Manager, verified this cycle): `grep "^\*\*Status"
  docs/adr/ADR-023-currency-display.md` → `Status: ACCEPTED (human, 2026-07-11; proposed
  2026-07-10)`.** Two dated addenda (2026-07-11) restore `show_code`/`code_placement` and
  supersede the "global toggle" framing — both implemented and verified below. This gap is
  now closed.
- **Human action item closed:** both ADR-022 and ADR-023 are now ACCEPTED (human,
  2026-07-11) — the architecture gate is fully closed for pixels and currency; no further
  action needed here. (ADR-025, the consent management ADR referenced throughout this
  update, is also ACCEPTED (human, 2026-07-11) — see `docs/releases/consent-management/
  RELEASE_CHECKLIST.md`.)

### Implementation complete
PASS for all three areas (qualitative + spot-checked against the ADRs, audits, and the
routed spec-review item list):
- **Pixels:** `pixels/` app (models, registry, 5 providers, 10 templates, admin card UI,
  slot provider), storefront wiring (product AJAX bridge, checkout `initiate_checkout`,
  thank-you purchase claim), `orders/migrations/0013_retire_purchase_guard_sentinel.py`.
  All routed spec-review items independently re-verified in current code:
  - **admin-018 card UI** — `pixels/templates/admin/pixels/pixel/change_list.html` exists,
    substantive (provider card grid, install/uninstall actions, installed badge), matches
    ADR-022 D8.
  - **PIX:P1 GA4 token strip** — `storefront/views_checkout.py:1696`
    (`page_url_override = request.build_absolute_uri('/orders/thank-you/')`) →
    `pixels/registry.py:76-98` (`render_base(pixel_id, page_url=...)`) →
    `pixels/templates/pixels/ga_base.html:12`
    (`gtag('config', ..., {'page_location': '{{ page_url|escapejs }}'})`). GA4-only, as
    decided; Facebook/TikTok/Snapchat/Pinterest unaffected (residual, see Known Risks).
  - **pixels race tests** — `pixels/tests/test_claim_race_integrity.py` is a new test file
    (beyond the pre-existing `test_concurrent_claim_race_yields_one_created_true` that the
    original `PIXELS_AUDIT.md` already credited) simulating a lost real-DB race via
    pre-inserted winning rows. Test count for the audit's exact command grew from **75 (at
    audit time) to 105 (re-run today)** — consistent with this file being added after the
    audit as a routed follow-up.
  - **`response.ok` branch (MEDIUM-1 fix)** — `storefront/templates/storefront/pages/
    product.html:200-209`: on `!response.ok`, falls back to `form.submit()`, does **not**
    dispatch the `pradize:pixels` event and does **not** navigate to `response.url`. Matches
    the audit's Fix recommendation exactly.
  - **`_json_script_escape` (LOW-1 fix)** — `pixels/registry.py:28-98`: a dedicated
    `_json_script_escape()` helper (documented as following Django's
    `_json_script_escapes`) wraps `content_ids_json` before `mark_safe`.
  - **`Pixel.save()` validation (LOW-2 fix)** — `pixels/models.py:80-108`: `save()` now
    re-checks `pixel_id` against the provider's `id_pattern` on every write path (not just
    the admin ModelForm), with a documented, deliberate narrower scope than `full_clean()`
    (explained in the docstring: preserves the ability to save existing rows whose provider
    plugin was later deregistered).
  - **`cart/views.py _safe_redirect` (LOW-4 fix)** — now delegates to Django's
    `url_has_allowed_host_and_scheme` instead of a hand-rolled `startswith("/")` check,
    closing the `/\evil.com` backslash-normalization bypass; docstring cites "PY-020
    hardening".
  - Findings **not** closed by design/deferred: MEDIUM-2 (thank-you token confidentiality)
    is addressed for GA4 only per the PIX:P1 human decision — the residual exposure for the
    other four providers is a named, accepted follow-up (ADR-022 dated addendum). LOW-5 (ADR
    D6 "never retro-fires" wording) — not verified fixed in this cycle; carried to Known
    Risks as a documentation-accuracy item.
- **Currency:** `currency/` app (`CurrencyDefinition`, `CurrencyRate`,
  `StoreCurrencySetting`, `CurrencyConverterSettings` singleton), `currency_tags` template
  tags (`display_price`, `currency_conversion_note`, `currency_picker`), ECB auto-refresh
  task (off by default), migrations `0001_initial`, `0002_seed_ecb_currencies`,
  `0003_currencydefinition_code_placement_and_more` (adds `show_code`/`code_placement`).
  Routed spec-review items verified:
  - **currency show_code (migration 0003)** — confirmed present, additive, both fields with
    sane defaults (`show_code=False`, `code_placement="suffix"`), no backfill required.
  - **currency emails guard** — new test file `currency/tests/test_email_conversion_boundary.py`
    (`EmailTemplatesNeverReferenceCurrencyConversionTest`) statically asserts no template
    under `emails/templates/` references `display_price`/`currency_conversion_note`/
    `currency_picker`/`{% load currency_tags %}`, mirroring the sibling checkout/thank-you
    guard (`currency/tests/test_checkout_conversion_boundary.py`). Confirmed
    `emails/templates/emails/order_confirmation.html` renders `{{ order.currency }}` directly
    (the store's transactional currency), never a converted display amount.
  - **currency docstring** — `currency/models.py` module docstring and
    `currency/templatetags/currency_tags.py::_format_amount` docstring both updated to
    document `show_code`/`code_placement` and the ADR-023 §2 addendum rationale.
  - ECB rate refresh: `CELERY_BEAT_SCHEDULE["currency-rate-refresh"]`
    (`webecom/settings/base.py:377`) registered but gated — the task itself checks
    `CurrencyConverterSettings.auto_refresh_enabled` (default `False`) before doing anything,
    confirmed via `currency/tasks.py:31-36` docstring ("always safe to have this task
    registered").
- **Chat:** `chat/` app (`ChatSession`, `ChatMessage`, `StoreChatSettings`, `StoreAiQuota`
  integration, `chat/tools.py` 4 read-only tools, `chat/anthropic_client.py`,
  `chat/system_prompt.py`, `chat/sessions.py`, `chat/quota.py`, `chat/admin.py`,
  `chat/slot_provider.py`, `chat/tasks.py` retention purge), widget
  (`storefront/templates/storefront/partials/chat_widget.html`). Routed spec-review items
  verified:
  - **chat AC-02/04/11/16 tests** (mapped via `specs/ecommerce_engine/10_implementation_tickets.md`
    lines 570-585, TICKET-038 AC-CHAT-01…16):
    - AC-CHAT-02 (acceptance audit) — `chat/tests/test_admin.py::test_save_model_stamps_
      timestamp_and_accepted_by_on_enable`, `test_disabling_later_preserves_the_acceptance_
      stamp`, `test_resave_by_a_different_admin_does_not_overwrite_existing_stamp`.
    - AC-CHAT-04 (read-only invariant, exact 4-tool set) —
      `chat/tests/test_tools.py::test_search_products_is_read_only`,
      `test_get_product_is_read_only`, `test_list_collections_is_read_only`,
      `test_get_store_policy_is_read_only`, plus an assertion the tool-name set is exactly
      `{search_products, get_product, list_collections, get_store_policy}`.
    - AC-CHAT-11 (quota, no AiJob/AiJobRun rows) —
      `chat/tests/test_views.py::test_successful_turn_creates_no_aijob_or_aijobrun_rows`
      (named explicitly in `AI_CHAT_AUDIT.md`'s re-audit as landed).
    - AC-CHAT-16 (prompt caching, ≥4096-token prefix, `cache_control` on last block) —
      `chat/tests/test_system_prompt.py::test_single_block_carries_cache_control_on_last_block`
      and the `_MIN_TOKENS = 4096` fixture; `chat/system_prompt.py:10-25` docstring
      documents the Haiku 4.5 minimum-cacheable-prefix invariant explicitly.
  - **chat-on-checkout suppression** —
    `storefront/templates/storefront/base_checkout.html:35`
    (`{% block chat_launcher %}{% endblock chat_launcher %}`) empties the slot on the
    distraction-free checkout page, matching CK-006 and the ADR-024 addendum's human
    decision 1. Note: the addendum text (as written) said this was "in progress... not yet
    merged" — **confirmed merged** in this review.
  - **chat_launcher rename** — `chat/slot_provider.py:39` (`slot = "chat_launcher"`),
    `storefront/templatetags/storefront_tags.py:59`
    (`PREVIEW_SUPPRESSED_SLOTS = frozenset({..., "chat_launcher"})`), and
    `storefront/slots.py:21` all use the renamed key; `storefront/tests/test_theme_
    conformance.py` (TH-132, 56 tests) re-ran green under the new key.
  - **ADR-024 ratification addendum** — verified present
    (`docs/adr/ADR-024-ai-sales-chat.md` lines 246-318), dated 2026-07-11, ratifying the
    policy-page FK modeling, session TTL, `KILLED` enum (documented as reserved, no dev
    follow-up), and the manual pricing table (condition carried to release-checklist, see
    below) — plus recording the two human decisions verified above.

### Tests added
PASS. Full project suite: **2623 tests** (re-run by Release Manager, see below). Area
breakdown (re-run independently):
- Pixels-relevant (`pixels storefront.tests.test_pixel_wiring
  orders.tests.test_pixel_sentinel_migration`): **105 tests** (up from 75 at the time
  `PIXELS_AUDIT.md` was written — the race-test follow-up landed after the audit).
- Chat-relevant (`chat`): **105 tests** (matches `AI_CHAT_AUDIT.md`'s re-audit count
  exactly).
- Currency-relevant (`currency`): **104 tests.**
- Theme conformance (`storefront.tests.test_theme_conformance`, TH-132, covers the
  `chat_launcher` slot rename): **56 tests.**

### Tests passing
PASS. Full suite re-run independently by the Release Manager just now:
```
python3 manage.py test
Ran 2623 tests in 86.858s
OK
```
Targeted re-runs, all green, zero failures/errors:
```
python3 manage.py test pixels storefront.tests.test_pixel_wiring orders.tests.test_pixel_sentinel_migration
Ran 105 tests in 7.601s
OK

python3 manage.py test chat
Ran 105 tests in 1.146s
OK

python3 manage.py test currency
Ran 104 tests in 0.352s
OK

python3 manage.py test storefront.tests.test_theme_conformance
Ran 56 tests in 0.322s
OK
```
(Console noise during the chat run — a deliberately simulated `TimeoutError` and one
"Pixel row ... has no matching registry entry" log line during the pixels run — are expected
test fixtures for the H1-timeout and orphan-provider-row regression tests respectively, not
failures; both runs report `OK`.)

### Coverage checked
Not separately re-measured with a coverage tool this cycle. Given 105/105/104 passing tests
for the three areas individually (spanning unit, integration, security-regression, and
race/concurrency suites) plus the full 2623-test suite green, coverage is judged adequate for
this gate; not a blocker. Same recommendation carried from the prior release: run an explicit
`coverage run/report` pass before the next major release.

### Spec Reviewer approved
PASS. The spec review's routed list of 10 follow-up items (as given to this Release Manager)
is verified **fully closed** in current code — see the itemized verification under
"Implementation complete" above: admin-018 card UI, currency `show_code` migration, chat
AC-02/04/11/16 tests, currency emails guard, pixels race tests, chat-on-checkout suppression,
`chat_launcher` rename, PIX:P1 GA4 token strip, ADR-024 ratification addendum, currency
docstring. No item was found unfixed or only partially fixed.

### Safety Agent approved
PASS for chat, CLEAR-WITH-NOTES for pixels — both independently re-verified, not taken on the
audit documents' word alone.

**Pixels — `docs/security/PIXELS_AUDIT.md` — verdict CLEAR-WITH-NOTES:**
- No CRITICAL or HIGH findings. Two MEDIUMs at audit time:
  - **MEDIUM-1** (`response.ok` never checked) — **verified fixed** in
    `product.html:198-224` (see Implementation section above).
  - **MEDIUM-2** (thank-you token confidentiality via pixel page_view URLs) — **partially
    addressed**: GA4 fixed via the PIX:P1 token-strip decision; Facebook/TikTok/Snapchat/
    Pinterest still transmit the real, non-expiring thank-you URL (no per-call override
    exists in their base snippets). This is an **honestly documented residual exposure**,
    not a silent gap — see `specs/ecommerce_engine/11_uncertainties_to_validate.md` lines
    100-115 and the ADR-022 dated addendum. Carried to Known Risks.
- LOW-1 (`content_ids_json` escaping), LOW-2 (`Pixel.save()` validation), LOW-4
  (`_safe_redirect` backslash bypass) — **all verified fixed** in code (see Implementation
  section). LOW-3 (duplicate-submit surface) — not verified fixed; optional hardening per
  the audit's own text, carried to Known Risks. LOW-5 (ADR D6 wording) — not verified fixed;
  carried to Known Risks as a documentation-accuracy item, not a functional risk.
- **The GDPR/ePrivacy consent gap is the headline item, not a "note".** Verified: no consent
  gate exists anywhere in `pixels/` — `PixelsSlotProvider.render()` fires immediately for any
  active, installed provider with no check of a consent signal. ADR-022 D7 documents this
  loudly as a cross-backlog blocker for EU launch. **This Release Manager treats it as the
  single biggest carried risk of this release — see the EU-launch position at the end of this
  document.**

**Chat — `docs/security/AI_CHAT_AUDIT.md` — re-audit (2026-07-11) verdict CLEAR-WITH-NOTES,
superseding an initial BLOCKED verdict:**
- Initial audit found 2 HIGH blockers (H1: no Anthropic client timeout; H2: policy-page FKs
  not store-scoped in admin or the tool layer) plus M1 (raw exception text to client), M2
  (cache atomicity dependent on Redis), L3 (pricing table Haiku-only).
- **H1 — verified fixed**: `chat/anthropic_client.py:75` passes
  `timeout=settings.CHAT_API_TIMEOUT_SECONDS` to the SDK; `CHAT_API_TIMEOUT_SECONDS` default
  30s, env-overridable (`webecom/settings/base.py:327`). Test:
  `chat/tests/test_anthropic_client.py::GetClientTimeoutTest`.
- **H2 — verified fixed at both layers**: `chat/admin.py` `StoreChatSettingsAdmin.
  formfield_for_foreignkey` scopes all 4 policy-page FKs to `request.store`; `chat/tools.py`
  `get_store_policy` independently re-checks `page.store_id == store.pk`, returning
  `not_found` + a logged warning on mismatch. Both layers test-pinned
  (`chat/tests/test_admin.py`, `chat/tests/test_tools.py::GetStorePolicyCrossStoreGuardTest`).
- **M1 — verified fixed**: `chat/anthropic_client.py`'s `_upstream_error_message()` returns a
  generic, translated message; raw exception text is confirmed (by test) never to reach the
  response body.
- **M2 (Redis-in-production) and L3 (pricing table)** — **ratified as named release-checklist
  conditions** in the ADR-024 addendum, not code fixes. Verified present as conditions below.
- **N1 (LOW, new in re-audit)** — mid-turn token spend unrecorded on error/disconnect;
  bounded, non-blocking, future ticket — carried to Known Risks.
- **Test-order flake** (`test_tools.py` + `test_slot_provider.py`, one-off) — investigated by
  the Safety Agent (report-only, no fix), **did not reproduce** in 3 consecutive re-runs plus
  this review's own full `chat` run. Most plausible cause named (a `ContentType` cache
  interaction with an unrelated `TransactionTestCase`'s full-flush teardown under a specific
  test-execution order), not implicated as a pixels/currency/chat functional defect. **No
  entry for this exists in `BUG_TESTS/BUG_TESTS.csv`** (checked directly — no
  `CHAT-CT-CACHE-FLAKE` or any chat-related row found), so it is not currently PROVEN,
  NOT_PROVEN, or otherwise tracked in that workflow; it remains an open, non-reproducing
  observation. Carried to Known Risks with a recommendation to the Test Agent for a targeted
  reproduction attempt.

### SEO Agent approved
N/A for this gate. None of the three areas ship new public/indexable pages: pixels render
inside the existing `slot.pixels` on already-indexed pages (no new URLs, no content
changes); currency conversion is a display-layer overlay on existing product/collection/cart
pages (no new URLs, `rel=canonical` unaffected — verified no new template introduces a
currency-specific URL param that would fragment canonicals); chat is a widget + two
non-indexable `/chat/session/` and `/chat/message/` POST endpoints with no crawlable surface.
No SEO Agent sign-off artifact found or expected for this release.

### Designer approved
N/A for this gate. Pixels render invisible third-party scripts (no visual surface beyond the
admin card UI, already reviewed under Spec Reviewer). Currency and chat both touch storefront
UI (currency picker, chat widget) but no Designer review artifact exists for this cycle and
none was requested in the task; both are functional, behavior-preserving additions to the
existing theme slot system (ADR-012), not a theme/visual-polish pass.

### Migrations reviewed
PASS. `python3 manage.py makemigrations --check --dry-run` → **"No changes detected"**
(re-run by Release Manager; exit 0) — model state matches the committed migration files for
every app, including the three new/changed apps in this release: `pixels` (new app),
`currency` (new app, 3 migrations including the `show_code`/`code_placement` addition), and
`chat` (new app). All are additive; `currency/migrations/0003` adds two new nullable-default
fields, no backfill required per its own migration file.

Note: as in the prior release, the local dev database (`db.sqlite3`) may show unapplied
migrations relative to the migration files — this is expected dev-environment drift (the
test runner builds its own migrated DB per run) and is not a release gate.

### Settings documented
PARTIAL PASS, with real gaps found (not previously flagged in either area's own audit doc).
- **CACHE_URL** — `webecom/settings/production.py` still requires it and fails loudly
  (`ImproperlyConfigured`) if absent (verified, unchanged from the prior release) — this now
  also gates chat's rate limits/budget atomicity (M2 condition) and pixels/currency have no
  cache-atomicity dependency of their own.
- **`redis` and `psycopg2-binary`** — **now present in `requirements.txt`** (`redis>=7.0`,
  `psycopg2-binary>=2.9`), closing the gap carried forward from the checkout-and-static-pages
  release. Verified directly by reading the file.
- **`anthropic>=0.40`** — present in `requirements.txt`, correctly commented as being for
  ADR-024/TICKET-038.
- **Chat settings** (`CHAT_KILL_SWITCH`, `CHAT_MODEL_ID`, `CHAT_SESSION_MESSAGE_CAP`,
  `CHAT_SESSION_TOKEN_CAP`, `CHAT_SESSION_TTL_SECONDS`, `CHAT_RETENTION_DAYS`,
  `CHAT_REPLY_MAX_TOKENS`, `CHAT_MAX_TOOL_ROUNDTRIPS`, `CHAT_API_TIMEOUT_SECONDS`) are all
  documented inline in `webecom/settings/base.py` with defaults and rationale comments.
- **Real gap found: `ANTHROPIC_API_KEY` is not validated at startup anywhere.**
  `chat/anthropic_client.py` relies on the SDK's own default env-var lookup
  (`anthropic.Anthropic()` reads `ANTHROPIC_API_KEY` internally); unlike `CACHE_URL` /
  `SECRET_KEY` / `PLATFORM_APEX_DOMAIN`, there is no `ImproperlyConfigured` check in
  `production.py` if it is missing. Practical effect: chat would deploy successfully and
  fail only on the *first real user message* (visible as a generic error to the shopper, per
  the M1 fix — not a silent failure, but also not a fail-fast deploy-time check). **Owner:
  Developer — add an explicit startup check, or accept and document as intentional (the
  kill switch / GDPR opt-in gates already prevent any call attempt on stores that haven't
  opted in).**
- **`manage.py check --deploy`** re-run by the Release Manager with a full env
  (`SECRET_KEY`, `DATABASE_URL`, `ALLOWED_HOSTS`, `CACHE_URL`, `PLATFORM_APEX_DOMAIN`)
  reports exactly **one** issue:
  ```
  WARNINGS:
  ?: (security.W009) Your SECRET_KEY has less than 50 characters...
  ```
  — the same dummy-`SECRET_KEY` artifact noted in the prior release, not a real production
  concern. No new deploy-check warnings from the pixels/currency/chat apps themselves.

### Deployment checklist ready
PARTIAL, same structural gap as the prior release: no single consolidated
`docs/DEPLOYMENT_HARDENING.md` exists. Deploy-level items for this release are itemized in
Known Risks and below instead:
1. Confirm production runs the Redis cache backend (already required by `CACHE_URL`) —
   this now also backstops chat's rate limits/daily budget atomicity (M2), not just the
   antispam system from the prior release.
2. Configure gunicorn worker count + `--timeout` appropriately for chat's SSE endpoint —
   `CHAT_API_TIMEOUT_SECONDS` (default 30s) bounds a single upstream call, but worker sizing
   for concurrent long-lived streams is still an ops decision (ADR-024 Risks).
3. Set `ANTHROPIC_API_KEY` in the production environment (see Settings gap above).
4. Verify the Anthropic DPA / sub-processor terms have been reviewed before any store owner
   is invited to accept the in-admin GDPR opt-in checkbox — the code enforces the checkbox is
   checked, but does not (and cannot) verify the legal DPA review happened; this is a
   business/legal process step, named explicitly as a condition in the task brief.
5. Populate `chat/quota.py::_PRICE_PER_TOKEN_USD` with a pricing row for any non-Haiku
   `model_id_override` **before** granting it to any store (ADR-024 addendum condition,
   ratifying L3).
6. EU-launch gate for pixels: do not enable any `Pixel` row for an EU-facing store until the
   cross-backlog GDPR/ePrivacy consent feature ships — see EU-launch position below.
7. **(Added 2026-07-11, from `docs/security/CONSENT_AUDIT.md` finding F3 — this item
   covers the consent feature that shipped after this checklist was originally written,
   not just the pixels/currency/chat trio above.)** Verify the edge reverse proxy
   restores the real client IP into `REMOTE_ADDR` before `POST /_consent/` traffic
   reaches Django. `pages.antispam.rate_limit_exceeded` keys the 5/hour consent-decision
   limit on `REMOTE_ADDR` only (by design — `X-Forwarded-For` is deliberately never
   parsed, since a client-controlled header would let an attacker spoof a fresh IP and
   defeat the limit). If the proxy does not restore the real IP, every shopper of a
   store shares one 5/hour bucket: after five decisions store-wide in an hour, all
   further shoppers get 429, the banner cannot be dismissed, and pixels stay dark
   platform-wide for that store (fail-closed, but a real availability defect) until the
   window rolls over. Same underlying edge-configuration item the antispam module
   already required for other public forms — now consent-critical too.

### Rollback plan ready
PASS — see `ROLLBACK_PLAN.md` in this directory.

### Known risks listed
PASS — see `KNOWN_RISKS.md` in this directory.

### Human decisions listed
PASS. Consolidated from `specs/ecommerce_engine/11_uncertainties_to_validate.md` (all
verified DECIDED where claimed) and the ADR-status gap found above:
1. ~~**ADR ratification** — ADR-022 and ADR-023 remain status `PROPOSED`; human should
   explicitly ACCEPT or object~~ **CLOSED (2026-07-11): both are now `ACCEPTED (human,
   2026-07-11)`, verified.** No further action.
2. ~~**GDPR/ePrivacy consent for pixels (EU launch)** — the single largest carried item~~
   **RESOLVED-by-ADR-025, CONDITIONAL (2026-07-11).** The consent management feature has
   shipped (`docs/releases/consent-management/`). See the rewritten EU-launch position in
   `KNOWN_RISKS.md` item 1 and the updated per-area verdict below for the exact conditions
   still open (deployment sequencing, legal sign-off on the beacon exemption, deploy-checklist
   live checks, and the RM-1 escape-hatch caveat) — no longer an unresolvable cross-backlog
   dependency, but not a blanket "done" either.
3. **PIX:P1 residual** — token-stripping works for GA4 only; Facebook/TikTok/Snapchat/
   Pinterest still transmit the token-bearing thank-you URL until server-side Conversions
   APIs ship. Accepted, documented, named as an ADR-022 follow-up.
4. **Chat production conditions** — Redis cache in production (M2), pricing-table rows before
   any non-Haiku `model_id_override` (L3), Anthropic DPA review before any store opts in,
   gunicorn worker/timeout sizing for SSE, `CHAT_KILL_SWITCH` as the documented instant
   rollback lever.
5. **Chat N1 (LOW)** — mid-turn token spend unrecorded on error/disconnect; future ticket,
   non-blocking.
6. **Currency ECB auto-refresh** — off by default; a super-admin must explicitly enable it
   per `CurrencyConverterSettings.auto_refresh_enabled`. The "approximate conversion, charge
   is always in store currency" disclaimer (`currency_conversion_note`) is mandatory and
   shipped — verified rendered whenever `display_currency != store_currency`.
7. **Chat test-order flake** — investigated, not reproduced, not yet in
   `BUG_TESTS/BUG_TESTS.csv`; recommend the Test Agent attempt the named reproduction order
   before closing it out either way.
8. **LOW-5 (pixels)** — ADR-022 D6's "never retro-fires" wording is factually wrong per the
   audit (late-fire on provider-install + thank-you revisit); needs a one-line ADR wording
   fix, not a code change.

---

## Per-area verdicts

### Pixel integrations (TICKET-030 / ADR-022)

- **READY FOR STAGING: YES.** All pixel-relevant tests green (105 tests, up from 75 at audit
  time), MEDIUM-1 and all LOW-1/2/4 findings verified fixed in code, migrations clean, no
  CRITICAL/HIGH security findings.
- **READY FOR PRODUCTION (non-EU stores): YES, WITH CONDITIONS.**
  1. ~~Human sign-off / ACCEPT on ADR-022 (currently PROPOSED)~~ **DONE — ADR-022 is
     ACCEPTED (human, 2026-07-11).**
  2. Correct ADR-022 D6's "never retro-fires" wording (LOW-5) — **Architect**, doc-only.
  3. Optional hardening (LOW-3, duplicate-submit surface) — **Developer**, non-blocking.
  4. Decide the disposition of MEDIUM-2's residual exposure for Facebook/TikTok/Snapchat/
     Pinterest (accept as-is vs. a future max-age on order-detail rendering) — **Human**,
     already partially addressed for GA4.
- ~~**READY FOR PRODUCTION (EU-facing stores): NO — BLOCKED.**~~ **UPDATE (2026-07-11,
  Release Manager): READY FOR PRODUCTION (EU-facing stores): YES, CONDITIONAL** — the
  cross-backlog GDPR/ePrivacy consent feature (TICKET-048/049, ADR-025) has shipped and been
  independently validated (`docs/releases/consent-management/RELEASE_CHECKLIST.md`).
  Conditions, per store, before enabling pixels on an EU-facing store:
  1. Consent (TICKET-048/049) deployed and active for that store
     (`ConsentSettings.is_enabled=True`, the default) — **Human/Product**, sequencing.
  2. Legal sign-off on the beacon-exemption reading (ADR-025 D4) obtained, or the
     conservative fallback (`beacon_requires_consent=True`) applied pending sign-off —
     **Human/Legal**, does not block deployment.
  3. Consent's own deploy-checklist live checks completed for the serving environment
     (real-IP restoration at the edge — shared with item 7 below; production cookie-flag
     verification) — **DevOps/QA**.
  4. **Newly discovered caveat (RM-1):** do not rely on the `non_eu_acknowledged` escape
     hatch for any store — it does not currently restore unconditional pixel firing (see
     `docs/releases/consent-management/KNOWN_RISKS.md` item 1). This does not affect the
     EU-readiness conditions above, which use the default `is_enabled=True` path (fully
     tested, unaffected). **Owner: Architect/Developer** for the fix.
  Absent condition 1 for a given store, that store's pixels remain NOT launch-ready for EU,
  unchanged from the prior posture. **Owner: Human/Product** overall.

### Currency display (TICKET-031 + TICKET-037 / ADR-023)

- **READY FOR STAGING: YES.** All currency-relevant tests green (104 tests), the
  emails/checkout conversion-boundary guards are tested and passing, `show_code`/
  `code_placement` shipped per the human-ratified addendum, migrations clean.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. ~~Human sign-off / ACCEPT on ADR-023 (currently PROPOSED)~~ **DONE — ADR-023 is
     ACCEPTED (human, 2026-07-11).**
  2. Confirm `CurrencyConverterSettings.auto_refresh_enabled` stays `False` at launch unless a
     super-admin has explicitly reviewed the ECB feed's reliability for the store's currency
     mix — **Human/Ops**, default is already safe, this is a go-live confirmation.
  3. No new dependency risk: ECB fetch is stdlib-only (per ADR-023), no new package to add to
     `requirements.txt`.

### AI sales-assistant chat (TICKET-038 / ADR-024)

- **READY FOR STAGING: YES.** Full `chat` suite green (105 tests, matching the re-audit
  count), both HIGH findings (H1 timeout, H2 store-scoping) verified fixed with tests, M1
  verified fixed, migrations clean, ADR-024 already ACCEPTED.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. Redis cache backend confirmed in production (M2 condition, ratified in the ADR-024
     addendum) — **DevOps**, shared with the existing `CACHE_URL` requirement.
  2. Pricing table (`chat/quota.py::_PRICE_PER_TOKEN_USD`) populated for any non-Haiku
     `model_id_override` before it is granted to a store (L3 condition) — **Developer**,
     before any such override is configured.
  3. `ANTHROPIC_API_KEY` set in the production environment, ideally with an explicit
     startup check added (currently silent-until-first-call) — **Developer/DevOps.**
  4. Anthropic DPA / sub-processor terms reviewed before any store owner is invited to accept
     the GDPR opt-in checkbox — **Human/Legal.**
  5. Gunicorn worker count + `--timeout` sized for concurrent SSE streams — **DevOps.**
  6. `CHAT_KILL_SWITCH` documented and rehearsed as the instant rollback lever — **DevOps**,
     see `ROLLBACK_PLAN.md`.
  7. N1 (mid-turn unmetered spend on error/disconnect) — schedule as a future ticket, not
     launch-blocking — **Developer.**
  8. Chase down the test-order flake reproduction (not yet in `BUG_TESTS.csv`) — **Test
     Agent / After-Bug Test Agent**, not launch-blocking (has not reproduced in any run so
     far, including this review's).

No test failures, no unresolved HIGH/CRITICAL security findings, no spec/implementation
mismatches were found in this review for any of the three areas. All routed spec-review
items (10/10) and all HIGH/MEDIUM security findings from both audits are independently
confirmed fixed or explicitly, honestly carried as accepted/scoped residual risk — except the
pixels GDPR/ePrivacy consent gap, which remains a real, undismissable blocker for **EU
launch specifically**.
