# TICKET-043 — French bookkeeping order-export API: decision questions

**Purpose:** unblock `11:#12` (CRITICAL). This is a question list for Cédric, not a spec draft. Once
answered, Spec Agent writes `13_bookkeeping_export_api.md` and the Architect writes an ADR.

**Why this is CRITICAL and not routine:** the engine is deliberately tax-free (DECIDED `11:#6` —
zero tax config, tax-inclusive prices, VAT appears *only* in this API's output). That means every
number needed to defend a French VAT return in a *comptable* audit has to be derived at export
time from data the engine does not currently model as tax-relevant. Getting the source of the VAT
rate wrong is a legal/financial-liability question, not a UI question.

---

## 1. VAT rate source per order line

**Context:** `OrderItem` stores `unit_price` tax-inclusive with no rate field anywhere (`orders/models.py`
has no `vat_rate` / `tax_class`). At export time the API must emit a rate (20% standard FR, 10%/5.5%/2.1%
reduced, or 0% for some goods/exports) per line.

**Question:** where does the rate come from?
- (a) a new `vat_rate` field on `Product`/`ProductVariant`, snapshotted onto `OrderItem` at order
  creation (same snapshot-rule pattern as `product_name`/`unit_price`) — most correct, most work.
- (b) a per-category default rate (`Category.default_vat_rate`), with per-product override.
- (c) one store-wide default rate + an exception list for reduced-rate products.

**Recommended default:** (a), because §6 requires *per-order-line* VAT breakdown and product catalogs
routinely mix standard/reduced-rate goods (food, books, etc. — foods theme is one of the 3 planned
themes). (b)/(c) under-model reduced-rate SKUs and would produce a wrong return if the catalog ever
mixes rates.

**What the engine currently lacks:** no rate field anywhere in `catalog` or `orders`. This is new
schema, not a read of existing data — plan it as such.

---

## 2. VAT regime values needed

**Context:** French export needs to classify each order/line into a regime (domestic FR, intra-EU
B2B reverse-charge, intra-EU B2C under OSS, export outside EU, DOM-TOM special cases). The regime is
derived from **ship-to country** (have — `Order.shipping_address['country']`, `ShippingCountry`),
**ship-from country** (do not have per-item yet, see Q4), and **buyer VAT-number status for B2B**
(do not have at all — no B2B customer/VAT-number field anywhere in `customers` app).

**Question:** which regimes do Cédric's actual stores need on day 1?
- Does any store sell B2B (would need a VAT-number capture field at checkout + reverse-charge logic)?
- Do any stores ship from outside the EU (dropship from China/US) as well as from FR/EU suppliers?
- Is DOM-TOM (Guadeloupe, Réunion, etc. — different VAT treatment) in scope?

**Recommended default (PENDING your answer):** if all current stores are B2C-only, start with 3
regimes — domestic FR, intra-EU B2C (OSS), export outside EU — and treat B2B reverse-charge and
DOM-TOM as out-of-scope-for-v1 (documented in `12_out_of_scope.md`) unless you say otherwise.

**What the engine currently lacks:** no B2B/VAT-number customer field, no regime enum anywhere.

---

## 3. Refunds representation

**Context:** refunds are modeled as `OrderCharge.refunded_amount` (cumulative Decimal per charge,
multi-charge model for upsell orders) plus `Order.payment_status` = `PARTIALLY_REFUNDED`/`REFUNDED`.
There is no credit-note concept and no per-line refund breakdown — refund amount is charge-level,
not line-item-level (confirmed in `orders/models.py` — no `OrderItem.refunded_amount`).

**Question:** does French bookkeeping need refunds as:
- (a) separate export rows (avoir / credit note) linked to the original order by `order_id` +
  `processor_charge_id`, with their own VAT breakdown (negative amounts, same rate/regime as the
  original line) — standard *avoir* practice; or
- (b) just a refunded-amount adjustment field on the original order's export row?

**Recommended default:** (a) — a comptable expects a credit-note line, and partial refunds (which
the engine explicitly supports, `PARTIALLY_REFUNDED`) cannot be represented as a single adjustment
if only some lines/quantities were refunded.

**What the engine currently lacks:** per-line refund amounts and per-line VAT rate (see Q1) — without
both, a partial refund's *avoir* cannot be broken down correctly by rate. If partial refunds in
practice are always "refund N whole units of item X," this is answerable from `line_total`/`quantity`
math; if refunds can be an arbitrary amount not tied to specific units (does the current refund
admin allow that?), this needs its own field. **Please confirm which refund UX exists/is planned.**

---

## 4. Per-item ship-from country at shipping validation

**Context (from `extra-spec-ecom.txt`):** *"we may have a default shipping address, when we validate
shipping of an order, we need to be able to change the from country (for one item only or up to all
items)"*. Operationally this reads as: dropshipping suppliers fulfill different line items from
different countries (e.g., one item ships from a France warehouse, another drop-ships from a China
supplier for the same order) — VAT/customs treatment differs per ship-from country, so the export
needs it per line, not per order. `05_database_schema.md` already anticipated a `ship_from_country`
field on `OrderItem`, but it does not exist in the current `orders/models.py` — it was deferred
pending this decision.

**Questions:**
- Is there a store-level or org-level **default ship-from country** today (e.g., the merchant's
  registered address, `Organization.registration_country`), applied to all items unless overridden?
- "At shipping validation" — does this mean when the admin creates an `OrderFulfillment` row (marks
  items as shipped), they can set/override `ship_from_country` per item at that moment? Should it be
  editable before that point too (e.g., while still `NOT_SENT`)?
- Should a change to `ship_from_country` be blocked once the order/fulfillment is already exported
  to bookkeeping (to avoid retroactively altering a filed VAT return)?

**Recommended default:** add `OrderItem.ship_from_country` (nullable, defaults to
`Organization.registration_country` at order creation), editable only up until the item's
`OrderFulfillment` is created (`FulfillmentRecordStatus.SHIPPED`); locked after export. Flag for
Architect: this needs an audit trail (who changed it, when) since it affects a filed tax return.

**What the engine currently lacks:** the field itself, the admin UI to edit it, and the "lock after
export" rule (which in turn requires an export-log/idempotency table — see Q7).

---

## 5. Export format / protocol

**Context:** ticket says "API." No target tool is named anywhere in the specs.

**Question:** what does your accountant/comptable actually consume?
- A REST endpoint the bookkeeping tool (Sage, Cegid, Pennylane, QuickBooks FR, or a comptable's own
  script) polls/pulls on a schedule?
- A CSV/FEC-format file the comptable imports manually?
- Something else (e-invoicing portal ahead of the 2026-2027 French e-invoicing mandate)?

**Recommended default:** REST endpoint (`GET /api/bookkeeping/orders/?period=...`), paginated,
JSON — token-authenticated per the ticket description — since "API" is explicit in the ticket title,
plus a CSV export button in admin as a manual fallback (cheap to add once the REST serializer exists).

**Please confirm:** which tool, and whether the French **e-invoicing/e-reporting mandate** (mandatory
B2B e-invoicing phased 2026-2027) applies to any of these stores — if these are B2C-only stores under
the reporting (not invoicing) obligation, requirements differ substantially and are lighter.

---

## 6. Period semantics, invoice date, and sequential numbering

**Context:** `Order.order_number` is unique **per store**, not necessarily sequential/chronological
across the store's full order history, and there is no invoice concept at all — per
`extra-spec-ecom.txt` line 23: *"Invoices are not generated. Email receipt only."*

**Legal flag (please read before deciding):** French bookkeeping law (art. A123-14 Code de commerce)
requires invoices to be numbered in an **unbroken chronological sequence with no gaps** when invoices
exist. Today this engine has no invoice document at all — only an email receipt — so this requirement
may not currently apply. But the moment this export API represents itself as *the* VAT-relevant
record, a comptable will likely expect either (a) a genuine sequential invoice number per order, or
(b) confirmation that no invoice is issued and the export is a sales-journal report, not invoices per se.

**Questions:**
- Is invoice date = payment date (`Order.captured_at`) for VAT purposes, or order date (`placed_at`)?
  France generally uses invoice/delivery date, which for tax-inclusive instant-capture ecommerce is
  usually the payment date — please confirm this matches your accountant's expectation.
- Do you need this API to *generate* a true sequential invoice number (new field/table), or is
  `order_number` + `created_at`/`captured_at` sufficient because your comptable treats the export as
  a periodic sales journal rather than individual invoices?

**Recommended default:** treat this as a sales-journal export keyed on `captured_at` (payment date),
not an invoice-numbering system — cheaper and matches "no invoices, email receipt only." But this is
a legal-compliance call, not an engineering one: **please confirm with your comptable/accountant**,
not just with this spec process.

---

## 7. Authentication for the API

**Context:** ticket says "Token-authenticated, org-scoped."

**Questions:**
- One long-lived token per Organization (rotatable in super-admin), or per-Store token?
- Does the consuming tool need push (webhook on new/refunded orders) in addition to pull, or is
  periodic polling by period range sufficient?
- Any rate limit / pagination size preference?

**Recommended default:** one revocable token per Organization stored in super-admin (mirrors the
existing `ProcessorAccount`-style credential pattern), pull-only via `?since=`/`?period=` query
params, no webhook push in v1 (add later if the comptable's tool needs it).

---

## Effort estimate once these are answered

| Item | Effort |
|---|---|
| Spec (`13_bookkeeping_export_api.md`) + ADR | 0.5–1 day (Spec Agent + Architect) |
| Schema: `vat_rate` snapshot field, `ship_from_country`, regime enum, org token, export-lock/audit | 0.5 day |
| Export endpoint + serializer + auth + pagination | 1–1.5 days |
| Refund/credit-note export rows | 0.5–1 day (depends on Q3 answer) |
| Admin UI: ship-from override at shipping validation | 0.5 day |
| Tests (rate/regime correctness, refund breakdown, auth, org-scoping, lock-after-export) | 1 day |
| Safety Agent review (financial/legal-adjacent, token auth, org-scoping) | 0.5 day |
| **Total** | **~4–5.5 developer-days** after this decision is closed |

This does not include e-invoicing-mandate compliance work if Q5's answer requires it — that would be
a separate, larger ticket.
