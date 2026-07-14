# 12_out_of_scope

> Out of scope — explicitly excluded features (with reason and revisit condition)

**Status: SEEDED — Spec Agent appends as exclusions are approved.**

| Excluded feature | Reason | Revisit when |
|---|---|---|
| Supplier database import/migration agent + supplier/factory marketplace features | Human decision (2026-07-02): build the reusable ecommerce engine first | Engine core is released and stable |
| Invoice generation | Human decision (extra-spec-ecom.txt): taxes shown as included in displayed price, email receipt only, no invoices | Explicit request |
| OpenAI API as default AI provider | extra-spec-ecom.txt: AI jobs run via Claude Code by default; OpenAI API is an opt-in super-admin setting, not default | N/A (design constraint, not a feature gap) |
| Invoice Orders module | Crossed out in red on menu screenshot (owner annotation) | Explicit request |
| Shipping Plus | Crossed out in red on menu screenshot | Explicit request |
| Zapier integration | Crossed out in red on menu screenshot | Explicit request |
| Taxes module | Crossed out in red; tax-inclusive display + email receipt only (see CRITICAL uncertainty #6 — confirm zero tax config) | French-bookkeeping API spec validation |
| CSV Templates | Crossed out in red on menu screenshot | Explicit request |
| Files module | Crossed out in red — but media/file upload must live SOMEWHERE in the new engine (MEDIUM uncertainty, Part A) | Architecture of media system |
| Product Variant report | Crossed out in red on reports screen | Explicit request |
| "Save View" on orders filter | Crossed out in red (note: saved views DO appear on issued-gift-cards filter — inconsistency to confirm) | Spec review |
| Separate General Settings and Domains screens | Owner annotation: MERGE into one screen (not excluded — merged) | N/A |
| Buy-X-get-X "Maximum value of free items" toggle + amount cap (visible in screenshot super-admin-11-04) | Human decision (2026-07-04): no free-value cap — the free item is always 100% free regardless of price | Explicit request |
