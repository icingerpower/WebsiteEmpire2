# Known Risks — Pixel Integrations + Currency Display + AI Sales Chat

**Date:** 2026-07-11. All items below were independently verified against current code/docs
by the Release Manager in this review cycle (not carried forward unverified).

**Update 2026-07-11 (Release Manager, consent-management release validation):** item 1 below
is **RESOLVED-by-ADR-025, CONDITIONAL** — no longer a blanket EU blocker. The consent
management feature (TICKET-048/049, ADR-025, `docs/releases/consent-management/`) has shipped
and been independently re-verified (Security Audit `CONSENT_AUDIT.md` v2 CLEAR-WITH-NOTES,
SEO Review `CONSENT_BANNER_SEO_REVIEW.md` PASS-WITH-FIXES, full suite 2757 tests OK). See the
rewritten item 1 for the new position and its exact conditions. **PIX:P1 (item 7 below,
GA4-only token-stripping residual) is unaffected and carried unchanged** — it is a
confidentiality residual orthogonal to consent legality, still open, still GA4-only.

## THE former headline item — now RESOLVED-by-ADR-025, CONDITIONAL (read this first)

1. **Pixels can now legally launch on EU storefronts — conditional on the consent feature
   being deployed and active.** `pixels/slot_provider.py::PixelsSlotProvider.render()` now
   gates every `Pixel` row on `consent.state.get_consent(request).allows(provider.
   consent_category)` (verified by direct code read this cycle) — undecided or refused
   shoppers get zero pixel snippets, granted shoppers get exactly the categories they
   consented to, and purchase/initiate-checkout claims are gated at the claim site (not only
   the render), so a late consent grant still fires the purchase pixel correctly on revisit
   instead of silently burning the claim. This closes the gap ADR-022 D7 named as a
   cross-backlog GDPR/ePrivacy blocker.

   **New position: pixels are EU-ready CONDITIONAL on all of the following holding for the
   specific store:**
   1. **The consent feature (TICKET-048/049) is deployed and active for that store**
      (`ConsentSettings.is_enabled=True`, the default for every store since migration) — a
      store must never have active `Pixel` rows without the consent gate live at the same
      time. Deploying pixels and consent as a single atomic release (or consent strictly
      first) is a named deployment condition, not optional sequencing.
   2. **Legal sign-off on the first-party analytics beacon's CNIL exemption reading (ADR-025
      D4) is obtained, or the conservative fallback
      (`ConsentSettings.beacon_requires_consent=True`) is applied for that store pending
      sign-off.** This is an external, still-open item (owner Human/Legal,
      `specs/ecommerce_engine/11_uncertainties_to_validate.md`) that does not block deploying
      the consent/pixels pair itself (the fallback needs no redesign) but does gate calling
      the EU position unconditionally clear.
   3. **The consent release's own deploy-checklist items are complete for the serving
      environment**: real-client-IP restoration at the edge proxy (shared with this
      release's own item 3/item 7 below — one physical check satisfies both), and a
      production cookie-flag / fresh-session live verification. See
      `docs/releases/consent-management/RELEASE_CHECKLIST.md` for the itemized evidence.
   4. **Known caveat, discovered during the consent release's validation (RM-1, see
      `docs/releases/consent-management/KNOWN_RISKS.md` item 1): the "this store does not
      target EU/EEA visitors" acknowledgment does NOT currently restore unconditional pixel
      firing** — it disables the banner but leaves that store's pixels permanently dark
      instead (fail-safe direction, but not the documented intent). This does not affect the
      EU-readiness conditions above (which rely on the default `is_enabled=True` path, fully
      tested and correct) — it only means the non-EU escape hatch itself should not be
      recommended to any store until fixed.

   **Absent condition 1 for a specific store (i.e. pixels active with no consent gate live),
   that store's pixels remain NOT launch-ready for EU, unchanged from the prior posture.**
   **Owner: Human/Product** for the sequencing/rollout decision; **Human/Legal** for condition
   2; **DevOps/QA** for condition 3; **Architect/Developer** for the RM-1 fix (condition 4).

## Deploy-config risks (block PRODUCTION, not STAGING)

2. **RESOLVED (2026-07-11).** ~~`ANTHROPIC_API_KEY` has no fail-fast startup check.~~
   `webecom/settings/production.py` now raises `ImproperlyConfigured` at import time when
   `not CHAT_KILL_SWITCH and not os.environ.get("ANTHROPIC_API_KEY")` — i.e. whenever chat is
   potentially servable (kill switch off) and no key is configured — mirroring the
   `CACHE_URL`/`SECRET_KEY`/`PLATFORM_APEX_DOMAIN` guards. A platform that keeps chat fully
   disabled via `CHAT_KILL_SWITCH` still boots without ever setting the key, preserving the
   per-store opt-in / platform-wide-optional invariant. Covered by
   `webecom/tests/test_production_anthropic_key_setting.py` (key absent + kill switch off →
   `ImproperlyConfigured`; key absent + kill switch on → no error; key present → no error;
   empty-string key treated as absent). **Owner: Developer.**
3. **Chat's rate-limit/budget atomicity requires Redis in production (M2, ratified as a
   named release-checklist condition in the ADR-024 addendum).** `chat/quota.py`'s
   `cache.add()+cache.incr()` calls are atomic only on Redis; on LocMemCache under multiple
   gunicorn workers, the effective cap becomes `N_workers ×` the configured limit — the exact
   same class of risk as the antispam F3/F4 concern from the prior release. `CACHE_URL` is
   already mandatory in `production.py`, so this is a re-confirmation, not a new gap, but it
   is a named condition specifically for chat's spend controls. **Owner: DevOps. Action:
   confirm no environment ever runs chat on LocMem/multi-worker.**
4. **Gunicorn worker sizing for concurrent SSE streams.** `CHAT_API_TIMEOUT_SECONDS` (default
   30s) bounds how long a single upstream call can pin a worker, closing the original H1
   unbounded-timeout risk — but sizing the worker pool for the expected concurrent-stream
   volume is still an operational decision, not something the code enforces. **Owner:
   DevOps.**
5. **Anthropic DPA / sub-processor review is a legal, not code, gate.** The code correctly
   enforces that a store owner must check the sub-processor acceptance box before chat can be
   enabled for their store (`_gating_refusal` checks `subprocessor_terms_accepted_at`), but it
   cannot verify that GRDF/the platform operator has actually reviewed Anthropic's DPA as a
   sub-processor before offering that checkbox to merchants. **Owner: Human/Legal. Action:
   complete before any store owner is invited to opt in.**
6. **Chat pricing table covers Haiku only.** `chat/quota.py::_PRICE_PER_TOKEN_USD` has one
   entry (`claude-haiku-4-5`); any store granted a pricier `model_id_override` will have its
   true API cost under-counted, silently weakening the monthly `StoreAiQuota` enforcement.
   Ratified as a named release-checklist condition in the ADR-024 addendum (not a code
   defect — the fallback-to-Haiku-rate behavior is the correct default and never blocks a
   reply). **Owner: Developer. Action: add a pricing row before granting any non-Haiku
   override.**

## Functional / correctness risks (not launch-blocking per the source audits, but real)

7. **PIX:P1 residual — token-stripping works for GA4 only.** The thank-you page's
   never-expiring order-access token is stripped from the pixel base snippet's automatic
   page_view URL only for GA4 (`gtag`'s documented `page_location` config override,
   `pixels/templates/pixels/ga_base.html:12`). Facebook, TikTok, Snapchat, and Pinterest have
   no equivalent per-call override in their base snippets and continue to read
   `document.location.href` — the real, token-bearing URL — for their automatic page_view
   calls. This is a real, honestly-documented residual confidentiality exposure (order items,
   payment summary, discount lines reachable by anyone with access to those ad platforms'
   accounts/exports), not a full fix. **Owner: Developer, named ADR-022 follow-up — closing
   step is server-side Conversions APIs (Meta CAPI, TikTok Events API, etc.) where
   `event_source_url` can be set explicitly server-side instead of collected client-side.**
8. **LOW-3 (pixels) — duplicate-submit surface in the AJAX add-to-cart bridge.** The `.catch`
   fallback re-POSTs via `form.submit()` even if the original fetch reached the server before
   the connection dropped (possible duplicate cart add); the submit button is not disabled
   during the in-flight fetch (double-click risk). Both are shopper-visible and
   self-correctable in the cart (remove the duplicate line); no security impact. **Owner:
   Developer, optional hardening, not launch-blocking.**
9. **LOW-5 (pixels) — ADR-022 D6's "never retro-fires" wording is factually wrong.** A
   provider installed after an order's first PAID thank-you render, followed by a shopper
   revisit of the (permanently bookmarkable) thank-you URL, **will** fire a late Purchase
   event for that newly-installed provider — the ADR's current wording claims this never
   happens. Not verified fixed in this cycle (no code or wording change found). Analytics
   integrity only (wrong-day attribution), no security impact. **Owner: Architect — correct
   the ADR-022 D6 wording; optionally add a recency-window guard (e.g. skip claims for orders
   older than 7 days) as a future hardening ticket.**
10. **N1 (LOW, chat) — mid-turn token spend unrecorded on error/disconnect.** `record_usage`
    runs only on the terminal `final` SSE event; a mid-turn error or client disconnect means
    tokens already consumed by Anthropic are never added to `ChatSession`/`StoreAiQuota`/
    `AiJobMetric`. Bounded by the pre-call message caps and per-IP/daily rate limits (worst
    case: unmetered spend capped at daily-budget × per-turn cost). **Owner: Developer, future
    ticket, not release-blocking.**
11. **Chat test-order flake, unreproduced.** A one-off failure was observed in
    `chat/tests/test_tools.py` + `chat/tests/test_slot_provider.py` during the Safety Agent's
    audit work. Investigated (report-only): did **not** reproduce in 3 consecutive re-runs,
    nor in this Release Manager's own full `chat` test run (105/105 green) or the full
    2623-test suite run (also green). Most plausible cause named in `AI_CHAT_AUDIT.md`: a
    `ContentType.objects.get_for_model()` process-wide cache interacting with an unrelated
    `TransactionTestCase`'s full-DB-flush teardown, under a specific (currently unobserved)
    test execution order — not a chat-specific bug. **Verified: no `CHAT-CT-CACHE-FLAKE` (or
    any chat-related) row exists in `BUG_TESTS/BUG_TESTS.csv`** — this item has not entered
    the bug-proven-test workflow and is not currently tracked as PROVEN or NOT_PROVEN there.
    **Owner: Test Agent — attempt the named reproduction order (pin
    `core.tests.test_store_scoped_manager` immediately before the chat test modules); if
    confirmed, the fix belongs in that test module's teardown
    (`ContentType.objects.clear_cache()`), not in `chat/`.** Not release-blocking; has never
    reproduced outside a single historical observation.
12. **Currency ECB auto-refresh is off by default — correct, but a go-live confirmation is
    still needed.** `CurrencyConverterSettings.auto_refresh_enabled` defaults to `False`
    (verified in `currency/models.py:217-219`); the beat-scheduled `currency-rate-refresh`
    task is safe to leave registered even with the flag off (it checks the flag first,
    verified in `currency/tasks.py:31-36`). Not a code risk, but a super-admin should
    consciously decide whether/when to flip it on rather than it silently staying off forever
    by omission. **Owner: Human/Ops, non-blocking.**
13. **JS runtime behavior has no automated browser harness (carried, unchanged from the prior
    release).** No `jest`/`playwright`/`cypress` files exist anywhere in the repo. The pixel
    AJAX bridge's `response.ok` branching, the chat widget's SSE consumption and
    `textContent`-only rendering invariant, and the currency picker's client-side behavior are
    covered only by template-level/Python-side assertions, not a real browser harness.
    Residual risk for anything that only breaks at the JS/DOM layer. **Recorded as a standing
    invariant in `AI_CHAT_AUDIT.md`: any future change to render chat output as markdown/HTML
    requires a new Safety pass, because model output can carry attacker-influenced text.**

## Human decisions still open (see RELEASE_CHECKLIST.md "Human decisions listed" for full detail)

14. ~~ADR ratification: ADR-022 and ADR-023 remain status `PROPOSED`~~ **UPDATE
    (2026-07-11, Release Manager, verified this cycle): both are now `ACCEPTED (human,
    2026-07-11)`** — confirmed via `grep "^\*\*Status" docs/adr/ADR-022-pixel-integrations.md
    docs/adr/ADR-023-currency-display.md`. This item is closed; no further human action
    needed (ADR-024 was already ACCEPTED).
15. **RESOLVED-by-ADR-025, CONDITIONAL (2026-07-11) — no longer open as a blocker.**
    GDPR/ePrivacy consent gating for pixels has shipped (`docs/releases/consent-management/`)
    — see the rewritten item 1 above for the exact conditions still requiring action
    (deployment sequencing, legal sign-off on the beacon exemption, deploy-checklist live
    checks, and the RM-1 escape-hatch caveat).
16. PIX:P1 residual disposition for the four non-GA4 providers (item 7) — accept as-is
    (current state) or fund the server-side Conversions API follow-up. **Unaffected by the
    consent release** — orthogonal confidentiality residual, still open.
17. Chat production conditions (Redis, pricing table, DPA review, worker sizing) — see
    RELEASE_CHECKLIST.md per-area verdict for owners.
