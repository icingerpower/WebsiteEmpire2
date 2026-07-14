# 00_product_positioning

> Product positioning — what Pradize is, target users, differentiation, core principles, and deployment context.

**Status: COMPLETE (2026-07-02).**

---

## 1. What Pradize is

Pradize is a multi-tenant Django e-commerce engine, self-hosted on a private VPS, designed to power storefronts that sell physical products. It is multilingual from day one: URL resolution, slug generation, menu rendering, hreflang tags, and AI-generated content all treat language as a first-class dimension, not an afterthought. The platform is organized into a two-level hierarchy — a super-admin control plane shared across all sub-stores, and a per-store admin for day-to-day operation — and is built and operated by a single developer-owner. Every autonomous operation in Pradize flows through an explicit job system: AI runners (Claude Code primary, Codex, Gemini Terminal) pick up queued jobs, produce outputs, and hold them for human review before anything is published. The architecture is shaped entirely by hard-won lessons from WebsiteEmpire2: visible failures, explicit state machines, atomic validation gates, and a single URL resolver are non-negotiable invariants baked in from the start.

---

## 2. Target users

### 2.1 The merchant (store admin)
The merchant is the owner of one or more sub-stores under the organization. They manage products, collections, orders, coupons, gift cards, shipping rules, and customer data through the per-store admin. They may run several language/country variants under one store. They interact with jobs indirectly: they review and accept or reject AI-generated outputs (descriptions, translations, timed promos, review-request emails) but they do not configure the job runners. They may also have employees — sub-accounts with a granular permission matrix — who act on their behalf.

### 2.2 The super-admin (platform operator — Cédric)
The super-admin is the sole platform operator. They configure everything that is shared across sub-stores: payment processor accounts and routing rules, shipping zones and carriers, organization-level coupon and gift card campaigns, employee permission templates, theme library, AI runner settings (Claude Code / Codex / Gemini Terminal; OpenAI API as opt-in only), currency and tax settings, Amazon affiliate IDs, and the global job system. The super-admin has read-only visibility into all per-store operations. There is currently one super-admin and that is intentional — this is an internal tool, not a SaaS product.

### 2.3 The end-customer (storefront shopper)
The end-customer visits a public storefront to browse products and collections, interact with the AI chat assistant, add items to cart, apply coupons or gift cards, and complete checkout. They see prices inclusive of all taxes. They receive email receipts (no invoices). They may be offered pre-order or quotation options for products not in standard inventory. They experience the platform through one of three curated themes (foods, fashion, general).

---

## 3. What Pradize is NOT

**Not a supplier marketplace.** Pradize manages the merchant's own inventory, including Amazon FBA availability and affiliate links. It does not facilitate multiple independent suppliers listing competing products on a shared storefront. Supplier DB import and migration are explicitly out of scope for the current phase.

**Not a SaaS product for third parties.** Pradize is an internal tool owned and operated by Cédric. There is no self-sign-up, no tiered pricing plan for external customers, no customer-facing billing. Merchants using it are invited, not self-provisioned.

**Not a thin Shopify or CommerceHQ clone.** It replicates the feature baseline of CommerceHQ (the platform being replaced) but adds a set of capabilities that no off-the-shelf platform provides in combination: first-party analytics with CTR, scroll depth, and purchase-rate tracking per collection slot; a structured AI job orchestration system with human review gates; a payment control plane with multi-organization routing as a decision tree (not a flat priority list); Amazon FBA inventory integration with affiliate buy buttons; multi-variant product pages for Pinterest content and A/B testing; an AI chat assistant; and three fully built themes. These are first-party, not plugin-dependent.

**Not generating invoices.** Tax is shown inclusive in all displayed prices. The system produces email receipts only. Invoice generation is out of scope.

---

## 4. Core principles

These principles are derived directly from the philosophy stated in `extra-spec-ecom.txt` and the hard-won lessons documented in `design-pattern-ideas.txt`. They are constraints, not goals — any feature design that violates them must be revised before implementation begins.

### 4.1 AI job system with human review gates
Every AI-generated output (translations, product descriptions, timed promos, email copy, social media posts, review-request emails) enters a review queue before it is published. AI runners — Claude Code (primary), Codex, Gemini Terminal — pick up queued jobs and produce drafts. The merchant or super-admin reviews, accepts, rejects, or requests revision. Automatic publication is the exception, not the default, and is explicitly configured per job type.

### 4.2 Visible failures — no silent corruptions
A failure that produces no exception, logs no error, and returns HTTP 200 is the worst kind of failure. Every async operation has an explicit status (PENDING / IN_PROGRESS / DONE / ERROR / SKIPPED). Every AI output is validated before persistence. Partial outputs are rejected, not silently accepted. Health dashboards surface job failures, routing anomalies, and empty collections before a human has to discover them manually.

### 4.3 Explicit state machines
Multi-step operations — AI generation pipelines, upsell funnels, abandoned-checkout campaigns, payment routing chains — are modelled as explicit state machines with named states, not inferred from the absence of records. "No record" means "unknown," not "not started." Every state transition is logged.

### 4.4 Single URL resolver
All components that need a URL (menus, breadcrumbs, hreflang, canonical, collection links, product links) call one resolution function. Nothing stores a pre-resolved URL in a field. The resolver is built once, consulted everywhere, and is the single source of truth for multilingual permalink lookup.

### 4.5 Test-first, maximum unit test coverage
Architecture is designed to maximize testability: complete test data sets, recorded API/event fixtures to reproduce real-life data, database migration tests. Every new job type, shortcode, and pipeline step ships with tests that cover the happy path, error states, and idempotency.

---

## 5. Differentiation vs CommerceHQ

CommerceHQ (running at `pradize.commercehq.com/admin`) defines the feature baseline to replicate. The following additions are owner-annotated requirements that CommerceHQ does not provide:

| Addition | Source |
|---|---|
| First-party analytics with per-collection-slot CTR, scroll depth %, and purchase rate — enables automatic product ordering improvements and job triggering | `extra-spec-ecom.txt`, screen_inventory part A |
| Structured AI job system: job queue, job types, human review gates, AI runner assignment (Claude Code / Codex / Gemini Terminal), job logging and health tracking | `extra-spec-ecom.txt` |
| Payment control plane: multi-organization routing as a decision tree (geography → split/threshold → processor strategy → fallback), decision log per order, running split counters for exact ratio tracking | `design-pattern-ideas.txt` §IX, screen_inventory part E |
| Amazon FBA integration: FBA inventory availability check, "Buy on Amazon" affiliate button with super-admin affiliate ID config | `extra-spec-ecom.txt`, screen_inventory part C |
| Multi-variant product pages: same product, different images, different URLs — for Pinterest content generation and A/B testing with basic statistics | `extra-spec-ecom.txt` |
| AI chat assistant on storefront — helps answer shopper questions and make product suggestions, backed by a product knowledge base | `extra-spec-ecom.txt` |
| Three fully built curated themes: foods, fashion, general — with different fonts, colors, and images; lightly customizable to reduce testing surface | `extra-spec-ecom.txt` |
| Per-language email templates managed via translation jobs — not a single template with token substitution | screen_inventory part D |
| Automatic redirect creation on slug change — no manual redirect entry required, redirect chains collapsed at write time | `design-pattern-ideas.txt` §XII, screen_inventory part B |
| "Free product" coupon type — not present in the CommerceHQ coupon editor | screen_inventory part C annotation |
| Auto-crop collection image previews | screen_inventory part B annotation |
| Customer-local-hour bucketing in the orders-by-hour report | screen_inventory part A annotation |
| Product-name text filter on the orders list | screen_inventory part A annotation |

---

## 6. Deployment context

- **Framework**: Django (Python), Qt-free, no dependency on the WebsiteEmpire2 Qt stack.
- **Hosting**: VPS 2 (dedicated Django e-commerce server), separate from VPS 1 (Drogon static sites). See the hosting architecture memory entry.
- **Multilingual from day one**: translated slugs, menus, product descriptions, email templates, and AI-generated content are all first-class. Language is a dimension of every URL, every model, and every job.
- **AI runners**:
  - **Claude Code** — primary runner for text jobs (translations, descriptions, promos, email copy).
  - **Codex** — secondary runner, better for code-adjacent or structured-output jobs.
  - **Gemini Terminal** — preferred for image and video jobs (social media content, product image enhancement, video generation via Veo).
  - **OpenAI API** — available as an opt-in setting in the super-admin only, not enabled by default. Never used without explicit super-admin activation.
- **Stats pipeline**: first-party, server-side. UTM parameters captured at first page view of session, attached to order at conversion. Pixel firing (Facebook, TikTok, Google) is idempotent via a `fired_pixels` table checked before every event.
- **Code generation**: the architecture is designed to support AI code generation for the application itself (Django views, models, job runners, tests) as an internal development accelerator.

---

## 7. Out of scope NOW

The following items are explicitly deferred. They must not be designed into the current architecture in ways that would be hard to remove; they equally must not be designed out in ways that would make them impossible to add later.

| Item | Reason deferred |
|---|---|
| Supplier DB import and migration | Requires a supplier data model and import toolchain not yet defined; out of scope for the initial engine build |
| Invoice generation | Pradize issues email receipts only; tax-inclusive pricing means formal invoices are not required at this stage |

---

*Source documents: `extra-spec-ecom.txt`, `design-pattern-ideas.txt`, `01_screen_inventory.md` (global findings).*
