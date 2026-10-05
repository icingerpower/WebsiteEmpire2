---
name: project-admin-polish-sweep
description: "Ongoing sweep to bring every admin changelist to the adminui standard (badges/buttons); reusable mixin + what's done/remaining"
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-05T22:20:49.783Z
---

Program (started 2026-09-05, Cédric): polish EVERY store admin changelist that
still shows raw Django boolean X/✓ icons — replace with `pa-badge` pills + styled
Edit button (+ per-row Publish toggle where the model has `is_published`). The
reference "good" look is the Product/Collection changelist.

**Reusable tool:** `core.admin.AdminUIListPolishMixin` — subclass declares
`polish_badge_fields` (+ optional `polish_badge_labels`); `__init_subclass__`
auto-generates `<field>_badge` sortable display columns (a badge method must NOT
share the boolean field's name — Django resolves the real field first and shadows
it). `edit_action` reuses `catalog.services.storefront_links.build_edit_link_html`.
The per-row publish toggle is the ADR-052 pattern (`CollectionAdmin` /
`StaticPageAdmin`): POST-only, CSRF, store-scoped/IDOR-safe queryset,
open-redirect-safe `next`. NOTE: a publish toggle that `save()`s directly bypasses
model `clean()` — StaticPage's toggle had to call `obj.clean()` before publishing
(placeholder-marker + reserved-slug guards).

**DONE:** Product, Collection, StaticPage (title_link + edit + publish toggle);
+ 8 via the mixin (badges+edit): DiscountCode, EmailTemplate, Review,
LeadCaptureCampaign, Campaign, ShippingRate-store, Bundle, SizeGuideCategory.
Commits `2b997d1` (StaticPage) + `01a3bbf` (mixin+8).

**Wave 2 super-admin DONE** (2026-09-06): 2a `6a84ff5` (catalog/discounts/currency/
emails/reviews super), 2b-1 `60fbde6` (shipping/payments/campaigns/aijobs super),
2b-2 `3f10dc4` (stores: Organization/Theme/Store/Employee/Domain/Language).
**i18n DONE** `790829d`: all 3 fr catalogs 100% (admin 643 / storefront 268 /
orders-console 154, 0 fuzzy/untranslated) — cleared the whole backlog.
Also `12d3b2e`: fixed 2 pre-existing red tests surfaced by the sweep — StoreAdmin
fieldsets now surface pwa_enabled/web_push_enabled/notify_customer_on_refund (were
uneditable), and a real latent migration bug (emails seed migrations 0007/0009/0011
referenced the current Store model instead of the historical one).

**RESOLVED:** `StoreCurrencySettingAdmin` — Cédric chose to KEEP the editable
`list_editable=["is_enabled"]` checkbox (no badge).

**STILL REMAINING (low value / needs a call):**
- Translation admin lists (`status` choice column) — badge would need a choice-field
  badge (mixin only does booleans); DEFERRED, low value (rarely browsed).
- `AiJobOutput.is_accepted` / `AiJobValidation.passed` have booleans but read as
  pipeline audit trails — left unbadged pending an architect/product call
  (read-only-lock vs polish).

**Out of scope:** inboxes (SentEmail/ContactMessage/…), singletons (settings),
readonly logs (payments/aijobs), auth (core.User), and the bespoke card-grid
admins `pixels.PixelAdmin` + `feeds.FeedConfigAdmin`. Relates to [[project_admin_redesign]].
