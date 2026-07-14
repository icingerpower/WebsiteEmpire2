# 01_screen_inventory

> Screen inventory — every screenshot/diagram in `/home/cedric/Dropbox/Applications/WebsiteEmpire2/spec-ecom/`, every visible and implied UI element.

**Status: FIRST PASS COMPLETE (2026-07-02).** 87 entries across 5 part files. Detailed per-screen entries live in `screen_inventory_parts/`; this file is the index and records the global reading conventions.

## Reading conventions discovered in the source material

- The admin screenshots are of an existing **CommerceHQ** store (`pradize.commercehq.com/admin`). They define the feature baseline to replicate.
- **Red annotations** on screenshots are the owner's change requests — they are requirements, not decoration.
- **Red cross-outs** mark features to EXCLUDE from the new engine (see `12_out_of_scope.md`).
- `admin-021-...-upsell-bumps-and-bundles*.jpg` screens are from **GrooveKart**, a different platform, used as reference material — replicate-exactly vs use-as-inspiration is an open human decision.
- The 5 SVGs describe the **Pradize Payment Control Plane** (super-admin): a designed-from-scratch multi-organization payment routing system, not an existing product.

## Part files

| Part | File | Entries | Scope |
|---|---|---|---|
| A | `screen_inventory_parts/part_A_admin_001-005.md` | 19 | Login, dashboard, menu, orders, all 13 reports |
| B | `screen_inventory_parts/part_B_admin_006-012.md` | 21 | Collections (smart + manual), gift cards, abandoned-checkout email wizard, customers, redirects, currencies, reviews |
| C | `screen_inventory_parts/part_C_admin_013-021.md` | 19 | Shipping feeds, purchase notifications, lead capture, general settings, product-page config, pixels, coupons, issued gift cards, product editor, upsells/bumps/bundles (GrooveKart), payment processing |
| D | `screen_inventory_parts/part_D_super_admin_jpg.md` | 23 | Employee accounts + 18-module permission matrix, security badges, checkout globals, shipping zones/carriers, automated emails (Twig-style templating), 5 up-sell archetypes, automated gift cards, theme library |
| E | `screen_inventory_parts/part_E_super_admin_svg.md` | 5 | Control-plane page map, organizations, processor accounts, two-stage routing rules (geography → split/threshold), payment methods |

## Global structural findings

1. **Two admin levels confirmed**: per-store admin (parts A–C) vs super-admin shared across sub-stores (parts D–E), matching the `extra-spec-ecom.txt` menu-split suggestion. Stores assign profiles and enable/disable payment methods locally; secrets and routing stay in super-admin.
2. **Payment routing is a decision tree**: enabled method → geography rule (return one org OR an org pool) → allocation rule (% split OR threshold switch) → processor option strategy (backup_chain / alternate_evenly) → default organization fallback. Matches design-pattern-ideas.txt §IX.
3. **Built-in analytics engine**: dashboard funnel, 13 report types, universal period-selector + CSV export toolbar, plus owner-requested event-tracking columns (collection/page display, CTR, purchase rate, scroll %) — implies a first-party impression/click/scroll event pipeline (matches extra-spec-ecom.txt collection statistics requirement).
4. **Coupons and gift cards appear to share one code system** (coupon editor has a literal "Gift card code" auto-generated field; 765 uniform $5 auto-issued cards) — needs architectural clarification.
5. **The jobs-to-do system is referenced by annotations across areas**: AI-generated reviews, AI-managed timed promos, email template translation via jobs — the job system is a cross-cutting foundation, consistent with extra-spec-ecom.txt making it "one of the first specs to define".
6. **Dual theme technologies** (Twig code themes + visual-builder themes) in an org-level library that sub-store admins choose from; the new engine will offer 3 curated themes (foods / fashion / general) per extra-spec-ecom.txt.
7. **Owner-annotated feature additions** recur across parts: Amazon FBA integration (inventory sync + "Buy on Amazon" affiliate button), multiple shipping price models per zone with product overrides + FBA-availability rule, "Free product" coupon type, per-language email templates managed by translation jobs, two-level campaign ownership (store admins see super-admin campaigns read-only and add their own), customer-local-hour report bucketing, hierarchical state reports, product-name order filter, auto-created redirects (manual creation removed), auto-crop collection image previews.

## Coverage

All 82 JPG screenshots and 5 SVG diagrams inventoried; the 2 text documents (`extra-spec-ecom.txt`, `design-pattern-ideas.txt`) are integrated into the workflow docs and agent instructions. Nothing skipped.
