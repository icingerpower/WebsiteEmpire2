# AI Sales Assistant — Technology Proposal (TICKET-038, uncertainty 11:#10)

**Status: APPROVED (human, 2026-07-10)** — all §8 decisions approved as
recommended (build now; stack as-is; GDPR per-store owner opt-in, OFF by
default). Superseded as the design authority by
`docs/adr/ADR-024-ai-sales-chat.md`; this document remains the option-analysis
and cost-model record.

**Author:** Architect Agent, 2026-07-10
**Scope:** Storefront sales-assistant chat with store knowledge base (spec 02/03,
TICKET-038, blocked by 11:#10). This document proposes the technology and
integration architecture; it contains no acceptance criteria and no production code.

---

## 1. The CLI-runner question — why chat cannot use the existing AI pipeline

Everything AI-generated in Pradize today goes through the AiJob queue and the
`run_ai_jobs` CLI runner loop (ADR-003 §7, ADR-014 §5): claim → heartbeat →
`build_prompt` → terminal runner (`claude_code` → `codex` → `gemini_terminal`,
`api` last) → validate → persist. The memory/discipline around this is strong
("all customer-visible content translated via AiJob CLI runner, never direct API").

**The rationale for that constraint is cost, not correctness.** ADR-003 §7 is
explicit: "terminal runners preferred … API calls are the last-resort fallback
when credits run low." Terminal runners ride flat-fee memberships; the API bills
per token. For batch content generation (descriptions, translations, SEO titles)
latency is irrelevant, so routing through the cheapest executor is free.

**Interactive chat breaks the precondition, not the principle.** A shopper
expects a first token in 1–3 seconds. The queue+runner path has polling
intervals, heartbeats, and a terminal process on another machine — tens of
seconds to minutes of latency by design. No amount of tuning makes a
poll-based CLI runner conversational.

Conclusion: **the CLI-runner constraint applies to content generation only;
chat must call the Claude API directly from Django.** What *must* carry over
from ADR-003 is the cost discipline the constraint was protecting:

- Chat spend is recorded per store into `AiJobMetric` (existing model, atomic
  `F()` updates) under a new `job_type='sales_chat'`, and counted against
  `StoreAiQuota` exactly like API-fallback jobs.
- When a store's quota is exhausted, the chat degrades explicitly (widget shows
  "assistant unavailable", contact form offered) — never silently (§XV-1).
- Any *batch* artifact the chat needs (see §4, optional store-context digest)
  goes through the normal AiJob registry and CLI runner.

---

## 2. Architecture options

### Option A — Claude API + embeddings RAG over catalog/policies

Embed products and policy pages into a vector store; retrieve top-k chunks per
user message; stuff into the prompt.

- Requires new infra we do not have: an embedding provider (Anthropic does not
  ship an embeddings endpoint — this means a second AI vendor, e.g. Voyage),
  a vector store (pgvector extension or an external service), and an indexing
  pipeline that must be kept in sync with catalog edits and translations.
- The payoff of RAG is scale: it wins when the corpus is too big to search
  structurally. A Pradize store has hundreds to low-thousands of products with
  clean structured fields (`Product`, `ProductVariant`, `VariantOption*`,
  `Collection`, translations) and a handful of `StaticPage` policy pages.
  SQL search over that beats approximate vector retrieval on precision
  (exact prices, exact stock, exact option values) — and hallucination risk is
  *lower* when the model reads authoritative rows instead of embedded prose.
- Staleness is a real failure mode: an embedded price is a wrong price waiting
  to be quoted.

**Verdict: rejected for v1.** Disproportionate infra for a small, structured,
per-store corpus. Revisit only if stores grow to tens of thousands of products
or free-text content (blogs, long guides) becomes the dominant knowledge source.

### Option B — Claude API + tool use over the existing catalog (RECOMMENDED)

The chat endpoint calls the Messages API with **read-only tools** that query
the live database, scoped server-side to the request's store:

| Tool | Backs onto | Returns |
|---|---|---|
| `search_products(query, max_results)` | existing catalog search (title/description `icontains`, collection names) | id, title, price range, availability, permalink |
| `get_product(product_id)` | `Product` + variants + options + stock | full details incl. per-variant price/stock |
| `list_collections()` | `Collection` | names + permalinks |
| `get_store_policy(kind)` | `StaticPage` (shipping / refund / terms — `POLICY` kind + generic pages) | published page text |

- **No new infra.** No vector DB, no embedding vendor, no sync pipeline. The
  model always reads current prices and stock.
- **Per-store scoping is structural, not prompt-based:** every tool executes
  `.for_store(request.store)`; the store id is never a model-supplied
  parameter, so a prompt-injected "show me store 7's data" is a no-op.
- **Anti-hallucination by construction:** the system prompt instructs the model
  to state prices, shipping terms, and discounts *only* from tool results, and
  the tools are the only source of those numbers in context.
- Tool definitions + system prompt + a short store digest form a stable prompt
  prefix that is prompt-cached (§6), so the per-turn token cost of carrying
  tools is small.
- Language: the chat endpoint runs under the storefront locale
  (ADR-008 `resolve_locale`); tools return translated fields where published
  records exist, falling back to source language (same rule as the storefront).

New models (implementation sketch, final schema in the ADR): `ChatSession`
(store-scoped, anonymous session key, locale, created/last-activity, message
count, token/cost counters) and `ChatMessage` (session FK, role, content,
tool-call trace JSON for audit). Both `StoreOwnedModel`. Retention: purge
sessions after N days (default 30, aligned with cart persistence) — relevant
to §7.

### Option C — Defer / descope

- **C1 — full deferral:** ship nothing; the contact/quotation forms (already
  live, antispam-hardened) remain the only assistance channel. Zero cost, zero
  risk; the extra-spec explicitly asks for a sales chat, so this only postpones
  the decision.
- **C2 — third-party widget (Crisp/Tawk/etc.):** fast, but no catalog
  grounding, per-store billing relationship with an external vendor, script-tag
  weight on every storefront page, and data flows we don't control. Conflicts
  with the settings-validation and per-store-scoping architecture for weak
  benefit.

**Verdict:** C2 rejected. C1 remains a legitimate *timing* choice for the human
(TICKET-038 is Phase 3 / P3): approving Option B's architecture now and
scheduling implementation in phase order costs nothing.

---

## 3. Transport

Constraint check (verified in repo): `webecom/asgi.py` is the stock Django
scaffold, nothing serves it; no `channels`/`daphne`/`uvicorn` in requirements;
production is WSGI + Redis cache + Celery. There is **no websocket
infrastructure**, and adding Channels means a new dependency, an ASGI server
in deployment, and a second process model to operate.

| Transport | UX | Infra cost | Notes |
|---|---|---|---|
| **A. Plain POST → full JSON reply** | Spinner for 2–8 s; no incremental text | none | Simplest; acceptable fallback |
| **B. Short-lived SSE per reply (RECOMMENDED)** | Token-by-token streaming | none new; ties up one worker thread for the seconds one reply streams | `POST /chat/message/` returns `StreamingHttpResponse` with SSE-formatted chunks from the Anthropic streaming API, then closes. Works under plain WSGI. Not a persistent connection — one stream per assistant reply. Requires threaded/async-capable workers (gunicorn `--threads` or gevent) sized for concurrent chats; document in the release checklist. |
| C. Long polling | Chunky pseudo-streaming | none | Worst of both; rejected |
| D. WebSockets (Channels) | Full duplex | **new dependency** (channels + channels-redis), ASGI server, deployment change | Overkill — chat is strictly request/response; nothing is server-initiated |

**Recommendation: B**, with A as the documented degradation path (same endpoint,
`?stream=0`). Streaming matters for perceived latency (first token ~1 s vs full
reply ~5 s), and short-lived SSE gets it without a single new dependency.
Widget side: vanilla JS `fetch` + `ReadableStream` reader — consistent with
T029-B (no build step); the widget occupies the theme system's chat slot
(ADR-012 component slots), rendered only when the store's chat settings are
valid and enabled (settings-validation rule: chat is a connector-like feature
with `required_settings`, `is_enabled()`, and launch-checklist visibility).

## 4. Knowledge base

Two sources, both already in the schema, both per-store by construction:

1. **Product catalog** — via the read-only tools of Option B. Live data; no
   build step; translations respected.
2. **Store policies** — `StaticPage` rows: the `POLICY` kind plus the
   shipping/refund/terms pages the static-pages area already produces
   (ADR-018). Served through `get_store_policy`, again live.

**The "knowledge-base build job" from the ticket** shrinks to one optional
batch artifact: a ~1–2 K-token *store digest* (store name, what it sells, tone,
shipping summary, active-policy one-liners) placed in the system prompt so the
assistant sounds like the store from the first message without burning tool
calls on basics. This is classic batch content generation, so it goes through
the **existing registry**: `register_job_type('chat_store_digest', ...)` with
`build_prompt` / `validate_output` / `persist_output`, executed by the CLI
runner, regenerated on relevant settings/policy changes (same trigger pattern
as `catalog` retranslation signals). v1 can ship without it (empty digest =
slightly more tool calls); it is an enhancement, not a dependency.

Per-store scoping is mandatory and is enforced at three layers: middleware
resolves `request.store` from the domain; tools filter `.for_store(store)`
with the store id taken from the request, never from model output; and
`ChatSession` is `StoreOwnedModel`, so the store-compliance test suite covers it.

## 5. Guardrails

- **Prompt injection via user messages.** The user can only influence the
  `messages` array, never the system prompt or tool bindings. Tools are
  read-only SELECTs — there is no action (no discount creation, no order
  changes, no email sending) for an injected instruction to trigger. Worst
  realistic outcome is the model saying something off-brand; mitigated by
  system-prompt hardening and the no-promises rule below. Tool *results* are
  our own DB rows rendered by our code (products/policies), so
  data-poisoning-via-tool-results reduces to "store owner wrote it", which is
  in-policy.
- **Price / promise hallucination.** System prompt rule: prices, stock,
  shipping times, and discounts may only be stated from tool results; the
  assistant must never invent a coupon, discount, or delivery promise, and
  must answer "I can't confirm that — please check the [policy page] / contact
  the store" when tools return nothing. Every assistant reply that mentions a
  product includes its permalink so the buyer lands on the authoritative page.
  The widget carries a permanent one-line disclaimer ("automated assistant —
  order details on product and policy pages prevail"). Post-hoc regex
  validation of numbers was considered and rejected for v1 (false-positive
  prone, and the tool-only-numbers instruction plus read-only design already
  removes the harmful cases: the bot cannot *apply* anything it invents).
- **Rate limiting.** Reuse `pages/antispam.py` exactly as designed
  (§XV-4: one primitive, N call sites): `rate_limit_exceeded(request,
  "chat_message", limit=~20, window=3600)` per (store, IP), plus a
  store-level daily message budget mirroring
  `contact_notification_budget_exceeded` (protects against distributed abuse
  that per-IP limits miss — same F4 lesson). Honeypot is N/A (JS widget), but
  the endpoint requires a session token minted server-side on widget load, so
  bare scripted POSTs without a page visit are rejected.
- **Per-store spend caps.** Two independent brakes: (a) `StoreAiQuota` —
  chat cost recorded per call via the existing `record_cost` path
  (`AiJobMetric`, atomic F() updates); quota exhausted ⇒ chat disabled with an
  explicit widget state, and the admin dashboard shows it (no invisible
  failure); (b) a hard per-session cap (max ~30 messages and a max-token
  budget per session) so one hostile session cannot drain a day's quota.
- **Input hygiene.** Message length capped (reuse the
  `PUBLIC_FORM_MESSAGE_MAX_LENGTH` idea, e.g. 2000 chars), history window
  truncated to the last N turns to bound token growth.
- **Safety review.** This area touches public input + external API + spend —
  it is explicitly flagged for the Safety Agent gate before release.

## 6. Cost model

Model pricing (Claude API, current as of 2026-07):

| Tier | Model id | Input /MTok | Output /MTok | Cache read | Notes |
|---|---|---|---|---|---|
| Haiku | `claude-haiku-4-5` | $1.00 | $5.00 | ~$0.10 | 200K context; **min cacheable prefix 4096 tokens** |
| Sonnet | `claude-sonnet-4-6` | $3.00 | $15.00 | ~$0.30 | 1M context; min prefix 2048 |
| Sonnet (new) | `claude-sonnet-5` | $3.00 ($2.00 intro to 2026-08-31) | $15.00 ($10 intro) | ~$0.30 | |

Assumptions per conversation: prefix (system prompt + tool definitions + store
digest) ≈ 4.5 K tokens — deliberately sized **above Haiku's 4096-token cache
minimum**, otherwise caching silently never engages; ~8 user turns; ~3 tool-use
round-trips ⇒ ~10 model calls; ~300 output tokens per call; prompt caching on
prefix + rolling history (5-min TTL matches active-chat cadence).

Per-conversation estimate (Haiku 4.5): cache writes ~6 K ($0.008) + cache reads
~50 K ($0.005) + uncached input ~5 K ($0.005) + output ~3 K ($0.015) ≈
**$0.03, budget $0.03–$0.06**. Sonnet-class ≈ 3× ⇒ **$0.10–$0.18**.

Monthly at plausible volumes (Haiku):

| Volume | Haiku | Sonnet |
|---|---|---|
| 300 conv/store/mo (small store) | ~$9–18/store | ~$30–55/store |
| 1,000 conv/store/mo (active store) | ~$30–60/store | ~$100–180/store |
| Platform, 20 stores × 500 conv | ~$300–600/mo | ~$1,000–1,800/mo |

**Model tier recommendation: Haiku 4.5** (`claude-haiku-4-5`). Chat-with-tools
over a small structured catalog is squarely Haiku-shaped work: short answers,
grounded lookups, low reasoning depth. The design keeps the model id a
platform setting (recorded per call in `AiJobMetric.model_id`), so a per-store
or global upgrade to Sonnet — e.g. as a paid store feature — is a config
change, not a code change. Escalation-per-message (Haiku triages, Sonnet
answers hard questions) is possible later but adds routing complexity for
unproven need; not in v1.

## 7. Data policy note — NAMED ITEM FOR THE HUMAN

Customer chat content (free-text messages, potentially containing names,
addresses, order references, health/size details for some verticals) is sent
to the Anthropic API. Consequences to acknowledge explicitly:

- **GDPR roles:** the store owner is controller, the Pradize platform is
  processor, and **Anthropic becomes a sub-processor** — it must be added to
  the platform's sub-processor list and covered by a DPA with Anthropic
  (Anthropic offers one; commercial API data is not used for training by
  default, standard retention applies — verify current terms at signature
  time).
- **Store-owner disclosure:** each store's privacy policy must disclose the
  AI assistant and the sub-processing; the widget should state it is an AI
  assistant at first open (also an emerging EU AI Act transparency
  expectation).
- **Data minimization:** the system prompt instructs the assistant to redirect
  order-specific/PII questions to the contact form rather than soliciting
  personal data in chat; `ChatSession` retention is bounded (default 30 days,
  configurable) and sessions are anonymous (no account linkage in v1).
- **Human decision needed:** confirm the platform is willing to take on the
  sub-processor relationship and the disclosure obligations, or direct that
  chat be gated per-store behind an owner opt-in acknowledging them
  (recommended: per-store opt-in checkbox in chat settings, off by default).

## 8. Decisions requested from the human

| # | Decision | Options | Recommendation |
|---|---|---|---|
| 1 | Architecture | A: API + embeddings RAG · **B: API + tool-use over catalog** · C: defer/descope | **B** |
| 2 | Transport | A: plain POST/JSON · **B: short-lived SSE over WSGI** · D: websockets (Channels) | **B**, A as fallback mode |
| 3 | Model tier | **Haiku 4.5** · Sonnet-class · per-message escalation | **Haiku 4.5**, model id configurable |
| 4 | Build now vs defer | Approve architecture + implement in Phase 3 order (ticketed P3) · approve and pull forward · defer decision itself | **Approve now, implement in Phase 3 order** |
| 5 | Data policy (§7) | Accept sub-processor role + disclosures · require per-store owner opt-in · block until legal review | **Per-store opt-in, off by default** |
| 6 | Store digest batch job (§4) | Include in v1 · later enhancement | **Later enhancement** |

On approval: Architect writes the ADR (chat models schema, tool contracts,
settings/launch-checklist integration, quota accounting) into
`specs/ecommerce_engine/09_architecture_decisions.md` + `docs/adr/`, and
TICKET-038's acceptance criteria are drafted against it.

**APPROVED 2026-07-10** — done: `docs/adr/ADR-024-ai-sales-chat.md` (design)
and TICKET-038 AC-CHAT-01…16 in `specs/ecommerce_engine/10_implementation_tickets.md`.
