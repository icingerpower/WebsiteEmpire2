# ADR-024: AI sales-assistant chat — direct Claude API with tool-use over the live catalog

**Status:** ACCEPTED (DECIDED-human 2026-07-10) — implements TICKET-038 per
`docs/proposals/ai-chat-technology-proposal.md` (all §8 decisions approved by the
human on 2026-07-10: build now; recommended stack as-is; GDPR per-store owner
opt-in, OFF by default).

**Extends:** ADR-003 (AI job system — scoped carve-out, see Decision 1),
ADR-008 (`resolve_locale`), ADR-012 (theme slot system — widget slot),
ADR-018 (static pages — policy grounding, antispam primitives).

**Binding constraints:** design-pattern-ideas §XV-1 (no invisible failures),
§XV-4 (one primitive, N call sites — antispam reuse), §XV-5 (validation at
persistence boundaries), §I (validate AI output before acting on it — here:
never persist/act, chat is read-only).

**Mandatory gate:** the Safety Agent MUST review this area before release —
public AI input surface (untrusted user text → external API → streamed HTML
context) + per-store spend. Named explicitly per the standard pipeline step 11.

---

## Decision

1. **Interactive chat calls the Claude API directly from Django** — a scoped
   carve-out from ADR-003 §7's terminal-runner-first rule. That rule's rationale
   is batch cost optimization (flat-fee memberships vs per-token API); it cannot
   apply to a surface needing a first token in 1–3 s. The carve-out is
   **interactive chat only**: every batch artifact (the optional
   `chat_store_digest` job, Decision 8) stays on the AiJob registry + CLI
   runner. ADR-003's cost discipline is preserved via Decision 6.
2. **New Django app `chat`** with three store-scoped models:
   - `ChatSession(StoreOwnedModel)` — opaque `session_key` (server-minted,
     signed), `locale`, `created_at`, `last_activity_at`, `message_count`,
     `prompt_tokens`, `completion_tokens`, `cost_usd`, `ended_reason`
     (explicit enum: `active` / `message_cap` / `token_cap` / `expired` /
     `killed` — never inferred, §XV-3 spirit).
   - `ChatMessage(StoreOwnedModel)` — `session` FK, `role`
     (`user`/`assistant`), `content`, `tool_trace_json` (audit of tool
     calls+results for the turn), `created_at`.
   - `StoreChatSettings(StoreOwnedModel)` — `is_enabled` (default False),
     `subprocessor_terms_accepted_at` (nullable), `accepted_by` (FK User,
     nullable), `model_id_override` (blank = platform default),
     `daily_message_budget` (default 500), unique per store.
3. **Knowledge access = tool use over the live DB, no RAG.** Four read-only
   tools, all executing `.for_store(request.store)` with the store taken from
   middleware — the store id is never a model-visible or model-supplied
   parameter:
   - `search_products(query, max_results≤5)` → id, title, price range,
     in-stock flag, absolute permalink.
   - `get_product(product_id)` → full detail incl. variants/options, per-variant
     price and stock, permalink. Cross-store id ⇒ not-found tool result.
   - `list_collections()` → names + permalinks.
   - `get_store_policy(kind)` → published `StaticPage` text for
     `shipping` / `refund` / `terms` / `contact` (resolved from the store's
     policy-kind and designated pages).
   Tool results are rendered by our code from our rows; translated fields are
   used when a published translation exists for the session locale, else
   source-language fallback (same rule as the storefront, ADR-008/ADR-014).
4. **Endpoints (storefront, WSGI, no new dependency):**
   - `POST /chat/session/` — mints a `ChatSession` + signed session token.
     Requires the storefront CSRF token. Refused (with explicit JSON reason)
     when: kill switch on, store not opted-in, quota exhausted, or rate limit.
   - `POST /chat/message/` — body: session token + one user message (≤2000
     chars). Returns a **short-lived SSE `StreamingHttpResponse`** for that one
     assistant reply (`event: status` pings during tool rounds, `data:` text
     deltas, terminal `event: done` with message id), then closes.
     `?stream=0` returns the full reply as JSON (documented fallback mode; same
     code path, buffered).
   - Conversation history is **rebuilt server-side** from `ChatMessage`
     (last 12 turns); the client never supplies history — prevents transcript
     forgery and token-stuffing.
5. **Model call.** Anthropic Python SDK, streaming Messages API, manual
   tool-use loop capped at **5 tool round-trips per user turn** (then the model
   is told to answer with what it has). Default model **`claude-haiku-4-5`**
   via Django setting `CHAT_MODEL_ID`, overridable per store
   (`model_id_override`). Prompt caching: `cache_control` on the last system
   block; the stable prefix (system prompt + store digest + tool definitions)
   MUST be ≥ 4096 tokens or padded documentation-style content added, because
   Haiku 4.5's minimum cacheable prefix is 4096 tokens — below it caching
   silently never engages. `max_tokens` per reply: 1024.
6. **Cost accounting (preserves ADR-003 discipline).** No `AiJob`/`AiJobRun`
   rows are created for chat calls — chat turns are not jobs. Instead, after
   every model call the `usage` block is recorded into **`AiJobMetric`**
   (`job_type='sales_chat'`, `model_id`, atomic `F()` counters — existing
   pattern) and added to **`StoreAiQuota.current_month_spent_usd`**. Before
   every model call: quota check (enforced budget exhausted ⇒ refuse before
   calling the API; widget shows the explicit "assistant unavailable" state —
   §XV-1, no invisible failure) plus session caps: **30 messages/session** and
   a session token budget (default 150K total tokens).
7. **Guardrails.**
   - Rate limiting reuses `pages/antispam.py` verbatim (§XV-4):
     `rate_limit_exceeded(request, "chat_message", limit=20, window_seconds=3600)`
     per (store, IP) on `/chat/message/`, `"chat_session"` limit 5/hour on
     `/chat/session/`, plus a store-level **daily message budget** counter
     (cache-based, keyed on store only — same shape as
     `contact_notification_budget_exceeded`) defaulting to
     `StoreChatSettings.daily_message_budget`.
   - System prompt rules (fixture, versioned in repo): prices, stock, shipping
     times and discounts may be stated **only** from tool results; never invent
     coupons/discounts/delivery promises; when tools return nothing, direct the
     buyer to the policy page or contact form; always include the product
     permalink when recommending a product; redirect PII/order-specific
     questions to the contact form; answer in the session locale.
   - Widget carries a permanent one-line AI disclaimer; product/policy pages
     prevail.
   - All tools are SELECT-only — there is no state-changing action for a
     prompt injection to trigger. Adding any write/action tool in the future
     REQUIRES a new ADR + Safety review.
   - Global kill switch: Django setting `CHAT_KILL_SWITCH` (env-driven)
     disables session minting and message handling platform-wide.
8. **Optional store digest (later enhancement, approved as such).** A
   ~1–2K-token store profile for the system prompt, generated as a registered
   AiJob type `chat_store_digest` on the **existing CLI runner** (build_prompt /
   validate_output / persist_output), regenerated on store-settings/policy
   change. v1 ships without it (empty digest ⇒ a few more tool calls); the
   system-prompt assembly reads it if present.
9. **Widget.** Rendered through the ADR-012 `SlotProvider` registry (chat
   slot), vanilla JS + plain CSS (T029-B: no build step), `fetch` +
   `ReadableStream` SSE reader with automatic fallback to `?stream=0` on
   stream failure. Rendered **only when** `StoreChatSettings.is_enabled` AND
   `subprocessor_terms_accepted_at` set AND kill switch off; quota-exhausted
   renders the unavailable state instead of hiding silently.
10. **GDPR opt-in (DECIDED-human).** Chat is OFF by default. The store admin
    settings page shows the sub-processor disclosure (customer chat content is
    sent to Anthropic; store must disclose in its privacy policy); enabling
    requires an explicit acceptance checkbox which stamps
    `subprocessor_terms_accepted_at` + `accepted_by`. Disabling keeps the
    acceptance timestamp (audit). `ChatSession`/`ChatMessage` retention:
    purged after `CHAT_RETENTION_DAYS` (default 30) by a Celery beat task.
    Sessions are anonymous — no customer-account linkage in v1.

## Context

TICKET-038 requires a storefront sales-assistant chat grounded in the store's
catalog and policies. Full option analysis, infra audit (plain WSGI, no
channels; Redis cache; Celery), cost model (~$0.03–0.06/conversation on Haiku
with caching) and the GDPR analysis live in
`docs/proposals/ai-chat-technology-proposal.md`; the human approved every
recommendation on 2026-07-10. This ADR turns the approved proposal into an
implementable design.

## Options considered

- **Architecture:** (A) Claude API + embeddings RAG over catalog/policies;
  **(B) Claude API + read-only tool-use over the live catalog**; (C) defer or
  third-party widget. (Proposal §2.)
- **Transport:** plain POST/JSON; **short-lived SSE per reply over WSGI**;
  long polling; websockets via Channels. (Proposal §3.)
- **Model tier:** **Haiku 4.5**; Sonnet-class; per-message escalation.
  (Proposal §6.)

## Chosen option

B + short-lived SSE + Haiku 4.5, with per-store GDPR opt-in (off by default),
as detailed in Decision 1–10.

## Why

- Per-store catalogs are small and structured; SQL tools beat approximate
  vector retrieval on price/stock precision, cannot serve stale prices, and
  need zero new infrastructure (no vector DB, no second AI vendor for
  embeddings, no sync pipeline).
- Short-lived SSE gives token-streaming UX on the existing WSGI stack with no
  new dependency; each stream lives only for the seconds of one reply.
- Haiku 4.5 is the right tier for grounded short-answer chat
  (~$0.03–0.06/conversation with caching); the model id is configuration, so
  upgrading a store to Sonnet is a settings change.
- Store scoping and read-only tools make the worst prompt-injection outcome
  "off-brand text", not data leakage or state change.
- The ADR-003 carve-out is justified by that rule's own rationale (batch cost),
  and its intent survives through `AiJobMetric`/`StoreAiQuota` accounting.

## Risks

- **WSGI worker occupancy under SSE.** Each streaming reply holds a worker
  thread for ~2–10 s. Mitigation: gunicorn threaded workers sized for expected
  concurrent chats (release-checklist item), the per-store daily budget bounds
  fan-out, and `?stream=0` fallback exists. Residual: acceptable at current
  scale; revisit before high-volume stores.
- **Residual hallucination.** Prompt rules + tool grounding + permalinks +
  disclaimer reduce but cannot eliminate wrong statements. Mitigation: chat is
  read-only (nothing it says is applied), disclaimer, and `tool_trace_json`
  audit per turn for dispute review.
- **Spend race under concurrent messages.** Quota check-then-call is not
  transactional across requests. Mitigation: monthly quota drift bounded by a
  few messages' cost; session caps and daily budget use atomic cache
  counters. Accepted.
- **Anthropic API outage/errors.** Streamed error event + logged failure +
  widget error state (never a blank success — §XV-1); contact form is the
  always-available fallback channel.
- **PII in chat content.** Anonymous sessions, redirect-to-contact-form rule,
  bounded retention, per-store opt-in with disclosure. Residual GDPR exposure
  documented in proposal §7 and accepted by the human via decision 5.

## Rollback strategy

- Feature is fully flag-gated: set `CHAT_KILL_SWITCH=1` (env) to disable
  platform-wide instantly — widget disappears, endpoints refuse with explicit
  reason; no deploy needed beyond env change/restart.
- Per-store rollback: toggle `StoreChatSettings.is_enabled`.
- Schema rollback: the `chat` app's migrations are additive (three new tables,
  no changes to existing tables) and reversible; `AiJobMetric` rows with
  `job_type='sales_chat'` are inert data.
- No storefront page depends on chat; removing the slot provider restores the
  pre-chat DOM exactly (ADR-012 slot contract).

## Tests required

1. **Opt-in gating:** widget absent and both endpoints refuse when
   (a) `is_enabled=False`, (b) terms not accepted, (c) kill switch on —
   each independently.
2. **Store scoping:** every tool filtered to the request store; `get_product`
   with another store's id returns not-found; `chat` models covered by the
   existing StoreOwnedModel compliance test.
3. **Read-only invariant:** test asserting the tool layer performs no
   INSERT/UPDATE/DELETE (query-count/assertNumQueries or connection-level
   write assertion).
4. **Transport:** SSE response streams incrementally under the test client and
   terminates with `event: done`; `?stream=0` returns the identical final text
   as JSON.
5. **Rate limits & budgets:** message 21 within the hour per (store, IP)
   rejected; store daily budget exceeded ⇒ rejected store-wide from a second
   IP; session message 31 rejected with `ended_reason='message_cap'`; session
   token budget exceeded ⇒ `token_cap`.
6. **Quota:** `StoreAiQuota` enforced-and-exhausted ⇒ refusal BEFORE any
   Anthropic call (mock asserts zero API calls); spend recorded into
   `AiJobMetric` (`sales_chat`) and `StoreAiQuota` after a mocked call with
   known usage numbers.
7. **History integrity:** client-posted history-like payload ignored; prompt
   assembled from the last 12 `ChatMessage` rows only.
8. **Input hygiene:** 2001-char message rejected; empty message rejected.
9. **Error path:** mocked Anthropic 429/500/refusal ⇒ user-visible error
   event, logged, session still usable (no invisible failure).
10. **Locale:** session on a `/fr` storefront gets French system-prompt
    language directive and French translated product fields in tool results
    (published-translation fixture) with source fallback when missing.
11. **Retention:** purge task deletes sessions older than
    `CHAT_RETENTION_DAYS`, leaves newer ones.
12. **Tool loop cap:** mocked model requesting endless tool calls stops at 5
    round-trips and still produces a final answer.

All Anthropic interactions in tests are mocked at the SDK boundary — no live
API calls in CI.

## Addendum (2026-07-11, Architect) — ratification of 4 flagged ASSUMPTIONS + 2 human decisions

Ratification pass following the spec-review/safety-audit cycle
(`docs/security/AI_CHAT_AUDIT.md`), verified against code as it stands.

1. **Policy-page FKs on `StoreChatSettings`** (`chat/models.py:19-26`,
   187-221) — **SOUND.** The modeling (4 nullable FKs onto `pages.StaticPage`
   inside the `chat` app) was always fine; H2's real objection was missing
   enforcement, now closed at both layers: `StoreChatSettingsAdmin.formfield_for_foreignkey`
   (`chat/admin.py`) scopes the dropdowns via `StaticPage.objects.for_store(store)`,
   and `get_store_policy` (`chat/tools.py`) independently re-checks
   `page.store_id == store.pk`, returning `not_found` + a WARNING log on
   mismatch instead of leaking. Both paths are tested
   (`chat/tests/test_admin.py`, `test_tools.py::GetStorePolicyCrossStoreGuardTest`).
   Keep both layers — the tool-layer check is what protects production if a
   future migration/shell write ever creates a cross-store FK directly.

2. **`CHAT_SESSION_TTL_SECONDS = 2h`** — **SOUND**, ratifying the safety
   audit's ACCEPTED verdict as-is. Reasonable idle window, env-overridable,
   fails closed (`EXPIRED`), independently bounded by the message/token caps.
   No change.

3. **Unused `ChatEndedReason.KILLED`** (`chat/models.py:48`) — confirmed dead:
   `chat_message_view`'s `_gating_refusal(store)` checks `CHAT_KILL_SWITCH`
   *before* the session row is even looked up, so a kill-switch refusal never
   touches a `ChatSession` to stamp. **Decision: document as reserved, no dev
   follow-up now.** Stamping would require loading the session ahead of the
   kill-switch gate solely to audit a platform-wide event that's already
   visible via the setting, the refusal reason, and logs — adds a DB
   read+write to the cheapest rejection path for marginal value, and still
   wouldn't cover sessions killed without a retry. `KILLED` stays in the enum
   as documentation of the full lifecycle state space (§XV-3 spirit); add a
   one-line "reserved, unassigned by design" note to its docstring at next
   touch of `chat/models.py`. A real per-session kill audit is new scope
   (batch-stamp on kill-switch flip, not per-refusal) requiring its own
   ticket.

4. **Manual pricing table** (`chat/quota.py:40-48`) — **SOUND, conditional**,
   ratifying ACCEPTED-WITH-NOTE (L3). One entry (`claude-haiku-4-5`) with
   fallback-to-Haiku-rate for unknown ids is correct default behavior for v1
   and the right failure mode (never block a reply over pricing). Condition
   carried forward as a **named release-checklist item: "chat pricing table
   covers every `model_id_override` in use"** — before any store gets a
   pricier `model_id_override`, `_PRICE_PER_TOKEN_USD` must gain an accurate
   row, or spend/quota enforcement silently under-counts. Non-blocking
   hardening suggestion: validate `model_id_override` against that table's
   keys at save time in `StoreChatSettingsAdmin`.

### Human decisions (2026-07-11) — recorded for traceability

1. **CK-006 (distraction-free checkout) wins over the chat widget.** Chat
   must not render during checkout. Gap confirmed in code: `base.html:74`'s
   `{% render_slot "chat" %}` is unlike the `overlay`/`social_proof` slots
   immediately above it — those are wrapped in blocks specifically so
   `base_checkout.html` can suppress them (CK-006); the chat slot currently
   is not, so it renders through checkout unsuppressed. Decision: wrap it in
   an overridable block (named `chat_launcher`, per decision 2) using the
   exact same pattern as `overlay`/`social_proof`, and have
   `base_checkout.html` override it empty. No new suppression mechanism.
2. **Slot renamed `chat` → `chat_launcher`**, per ADR-012 D9 and spec-15
   TH-045 (which already documents `slot.chat_launcher`, T038/P3). Code
   predates that naming (`chat/slot_provider.py:31`, `chat/apps.py`,
   `base.html:74`) and must be brought into line; touches
   `storefront/tests/test_theme_conformance.py` (TH-132) — confirm it still
   passes under the new key.

Both are implemented together by the Developer agent (in progress at time of
writing, not yet merged): the block introduced for decision 1 should be named
`chat_launcher` directly, not `chat`, to avoid a second rename of the same
line. This ADR's Decision 9 "chat slot" wording and the `chat/slot_provider.py`
docstring are superseded by `chat_launcher` — update at the same time as the
code so the ADR doesn't go stale. No new ADR required; this addendum is the
decision record.
