# ADR-018: Static pages + store-level contact/quotation forms

**Status:** ACCEPTED (human, 2026-07-10) — implements TH-088 (ADR-012 Phase 2 item: `pages/static_page.html`
+ contact/quotation forms, no-JS POST).
**Extends:** ADR-005 (PermalinkResolver, redirect table), ADR-008 (locale resolution), ADR-012
(storefront engine, D5 dispatcher, D6 seo_head, locked template set), ADR-014 (translation
pipeline, staleness fingerprints, slug_hint).
**Spec:** `specs/ecommerce_engine/15_storefront_theme_system.md` TH-088, TH-063 (no-JS forms),
NFR-T3 (`|safe` policy); `07_multilingual_seo.md` ML-002/ML-011, CAN-005/CAN-007, SM-010.
**Binding constraints:** design-pattern-ideas §II (NFD slugs — reuse `core.slugs.make_slug`),
§III (single permalink resolver; menu items store object refs, never URLs), §XI (hreflang in one
place; legal pages get a no-op), §XII (redirects on slug change), §XV-1 (no invisible failures),
§XV-4 (single resolution function), §XV-5 (validation at persistence boundaries).

---

## Context

The `<path:slug>` catch-all (`storefront/urls.py` → `page_view`, `storefront/views.py:374`)
dispatches through `permalinks.registry.resolve_storefront_path`, which today knows two content
types: `product` and `collection`. ADR-012's template inventory promises
`pages/static_page.html` and the B2B theme emphasizes a quotation form, but no StaticPage model
exists. Product-anchored quotation already works (`catalog.QuotationRequest` +
`/products/<slug>/quotation/`, `storefront/views_product_forms.py`); there is no store-level
contact surface, no legal/policy pages (Stripe/PayPal review requires reachable terms + refund +
privacy URLs), and the footer/header nav partials contain only Phase-2 placeholder comments.

Facts this ADR builds on (verified in code):

- Translation pattern: `ProductTranslation` (`catalog/models.py`) — per-lang row with
  `status draft/published`, `ai_job` FK, `manually_edited_at`, `source_fingerprint`, `slug_hint`;
  publish/unpublish drives Permalink activate/deactivate via `catalog/signals.py`
  `_sync_translation_permalink`; AiJobs are queued per enabled non-default `StoreLanguage` with
  staleness + flood guards.
- `catalog.signals.detect_slug_change` is an **unfiltered** `pre_save` receiver — any model with
  `slug` + `pk` already emits `slug_changed`; only the consumer
  (`permalinks/signals.py handle_slug_change`) branches per model.
- Product descriptions render **raw HTML with `|safe`**
  (`storefront/templates/storefront/pages/product.html:141,145`); no sanitizer exists anywhere;
  `requirements.txt` has no bleach/nh3/markdown and dependency additions need human approval.
- No rate-limiting infrastructure exists (analytics ingest is fire-and-never-raise, no throttle).
  `CACHES` is LocMem in dev, Redis in production (`webecom/settings/base.py`).
- `emails.service.send_transactional_email()` is the single, never-raising email entry point with
  per-store template override + file-based defaults.
- Sitemaps (`sitemaps/sitemaps.py`) iterate **active Permalinks** — a new content type is included
  automatically once it registers Permalinks (SM-010 already names static pages).
- Store creation happens in super-admin `save_model`; no post-save initialization hook exists.
- Store-admin ModelAdmin pattern: register on `store_admin_site`, `exclude = ("store",)`, set
  `obj.store = request.store` in `save_model`, scope `get_queryset` by `request.store`, gate with
  `_check_module_access(request, "<module>", "limited|full")`.

---

## D1 — `StaticPage` model in a new `pages` app, wired into the existing permalink machinery

**Options considered:**
(a) models in the `storefront` app; (b) models in `catalog`; (c) new `pages` Django app;
(d) models in `stores`.

**Chosen: (c) — new `pages` app** owning `StaticPage`, `StaticPageTranslation`,
`ContactMessage`, their signals, admin, the nav template tag (D6), the seeding command (D5) and
the anti-spam helper (D3).

**Why:** `storefront` is deliberately model-free — it is the rendering engine (ADR-012 D1) and
must stay that way; `catalog` is commerce objects (products/collections/variants) and already at
954 lines; `stores` is tenancy/config. Static pages are store content with their own translation
lifecycle — exactly one cohesive app. The dispatcher and templates stay in `storefront`
(views render content; content lives in `pages`), mirroring how `catalog` models are rendered by
`storefront` views today.

### Schema

```python
# pages/models.py
class StaticPageKind(models.TextChoices):
    GENERIC   = "generic",   "Generic content page"     # About, FAQ…
    CONTACT   = "contact",   "Contact form page"
    QUOTATION = "quotation", "Quotation form page"
    POLICY    = "policy",    "Legal / policy page"      # Terms, Privacy, Refund, Shipping

class StaticPage(StoreOwnedModel):
    title    = models.CharField(max_length=512)
    slug     = models.SlugField(max_length=512, blank=True)   # make_slug(title) in save(), §II
    body     = models.TextField(blank=True, default="")       # raw HTML — D2
    kind     = models.CharField(max_length=16, choices=StaticPageKind.choices,
                                default=StaticPageKind.GENERIC)
    is_published   = models.BooleanField(default=False)
    seo_title       = models.CharField(max_length=255, blank=True, default="")
    seo_description = models.TextField(blank=True, default="")
    show_in_header = models.BooleanField(default=False)        # D6
    show_in_footer = models.BooleanField(default=False)        # D6
    nav_position   = models.SmallIntegerField(default=0)       # ascending within each zone
    published_at   = models.DateTimeField(null=True, blank=True)  # set on first publish (parity
                                                                  # with Product; §XII 7-day rule later)
    created_at / updated_at

    class Meta:
        unique_together = [("store", "slug")]
        indexes = [models.Index(fields=["store", "is_published"])]

    @property
    def hreflang_exempt(self) -> bool:      # §XI legal no-op — single source (see SEO below)
        return self.kind == StaticPageKind.POLICY

class StaticPageTranslation(StoreOwnedModel):
    """Byte-for-byte the ProductTranslation pattern (catalog/models.py) — same fields,
    same lifecycle, same admin behavior."""
    page       = models.ForeignKey(StaticPage, on_delete=models.CASCADE,
                                   related_name="translations")
    lang_code  = models.CharField(max_length=10)
    title      = models.CharField(max_length=255, blank=True)
    body       = models.TextField(blank=True)
    seo_title  = models.CharField(max_length=255, blank=True)
    seo_description = models.CharField(max_length=500, blank=True)
    status     = models.CharField(choices=TranslationStatus.choices, default=DRAFT)
    ai_job     = models.ForeignKey("aijobs.AiJob", null=True, blank=True,
                                   on_delete=models.SET_NULL,
                                   related_name="static_page_translations")
    manually_edited_at = models.DateTimeField(null=True, blank=True)
    source_fingerprint = models.CharField(max_length=64, blank=True, default="")
    slug_hint  = models.CharField(max_length=255, blank=True, default="")
    created_at / updated_at
    # clean(): store must match page.store (same guard as ProductTranslation.clean)

    class Meta:
        unique_together = [("store", "page", "lang_code")]
```

`TranslationStatus` is imported from `catalog.models` (do not duplicate the enum).
Fingerprint field order (must be documented on the model, order-sensitive per
`translation_fingerprint`): `(title, body, seo_title, seo_description)`.

### Reserved top-level slugs (new, required)

Static page slugs are **root-level** (`/about-us/`, `/contact/`) — same namespace as products,
clean URLs for policy links. Fixed routes in `storefront/urls.py` are registered before the
catch-all, so a page slugged `cart` would silently never resolve (§XV-1 invisible failure).
Add to `permalinks/models.py`, next to `_validate_slug_not_reserved`:

```python
RESERVED_TOP_LEVEL_SLUGS = frozenset({
    "cart", "checkout", "search", "orders", "products", "collections",
    "admin", "campaigns", "sitemap.xml", "robots.txt", "_analytics",
})
```

Enforced in the same validator (reject when the first path segment is reserved), so it protects
**every** permalink producer — static pages, products, and AI slug_only jobs — in one place
(§XV-4). Existing product slugs are unaffected at runtime (validators run on full_clean/admin
paths); the migration adds no data change.

### Permalink lifecycle (mirrors catalog exactly)

All in `pages/signals.py`, connected in `PagesConfig.ready()`:

1. **Publish/unpublish (source language):** `post_save` on `StaticPage` — when
   `is_published=True`, create-or-activate the Permalink for
   `(store, store default lang, slug)` with `auto_created=True`; when `False`, deactivate all
   permalinks for the page (mirror of `permalinks/signals.py sync_permalink_on_status_change`).
   Path convention: **path = slug** (root-level, like products — no `pages/` prefix).
2. **Translations:** `pre_save`/`post_save` on `StaticPageTranslation` delegating to the existing
   `catalog.signals._sync_translation_permalink` (it is already generic — takes
   `content_model_class`, `object_id`, `slug_factory`). Slug factory: translated title →
   `make_slug`, fallback to source slug. Publishing a translation activates the translated
   permalink; reverting to draft deactivates it (404, AC-104).
3. **Slug changes:** `catalog.signals.detect_slug_change` already fires for StaticPage (unfiltered
   pre_save). Extend `permalinks.signals.handle_slug_change` with a `StaticPage` branch
   (`old_path = old_slug`, `new_path = new_slug` — root-level like Product). Redirect + chain
   collapse + loop rejection come for free (§XII).
4. **AiJob translation triggers:** `post_save` on `StaticPage` when `is_published=True` — one job
   per enabled non-default `StoreLanguage`, `sub_type="static_page"`, payload
   `fields={title, body, seo_title, seo_description}` + `length_limits={seo_title: 60,
   seo_description: 160}`, using the existing helpers from `catalog/signals.py`
   (`_active_non_default_languages`, `_get_default_lang`, `_flood_guard`,
   `aijobs.service.create_job`) and a `_should_queue_static_page_translation` staleness check
   mirroring `_should_queue_product_translation`. Extract the shared helpers into
   `aijobs/triggers.py` if the Developer prefers, but do not fork their logic. Long bodies are
   the AiJob runner's chunking concern (§I), not this ticket's.

### Dispatcher + view

- `permalinks/registry.py resolve_storefront_path`: add a `staticpage` branch (the function is
  documented as "the ONLY place that dispatches content-type → view" — this is the sanctioned
  extension point). Load via `StaticPage.objects.for_store(store)`, call
  `storefront.views_pages.static_page_view(request, page)`.
- New `storefront/views_pages.py` (keeps `views.py` from growing): `static_page_view` renders
  `storefront/pages/static_page.html` (engine-owned, locked — themes cannot override it,
  ADR-012 D3). Language rule:
  - request lang == store default lang → serve `StaticPage` source fields;
  - otherwise → require a **published** `StaticPageTranslation` for the lang, else 404 (ML-002;
    permalink deactivation makes this mostly unreachable, keep as defensive parity with
    `product_page`).
  - `is_published=False` → 404 (defensive; permalink should already be inactive).
  - Context: `content_object=page` (so base.html's `{% seo_head %}` renders canonical/hreflang),
    `page_type="static_page"` (analytics beacon), breadcrumbs Home → page title via
    `_build_storefront_url`.
- `GET` renders; `POST` is accepted **only** when `kind` is `contact` or `quotation` (D3/D4) —
  other kinds return 405. Forms POST to the page's own URL through the catch-all (no new fixed
  routes, no-JS friendly, PRG redirect to `?sent=1` on success). CSRF middleware applies as-is.

### SEO integration points (named, per constraint)

- **Canonical + og:locale:** free — `{% seo_head content_object %}` in base.html (ADR-012 D6),
  fed by `content_object=page`.
- **Hreflang:** free for `generic`/`contact`/`quotation` pages (Permalink-driven,
  `permalinks/hreflang.py`). **Policy pages get the §XI legal no-op:** the `{% hreflang_tags %}` /
  `{% seo_head %}` tag checks `getattr(content_object, "hreflang_exempt", False)` and renders no
  alternates when True. `hreflang_exempt` derives from `kind` — one source field.
- **Sitemap:** inclusion is automatic (Permalink-driven, SM-010). One change in
  `sitemaps/sitemaps.py PermalinkSitemap.get_urls`: fetch the store's policy-kind StaticPage PKs
  (one query) and clear `alternates` for those `(content_type, object_id)` keys — sitemap and
  page-level hreflang must derive from the same `kind` field so they can never disagree
  (CAN-005). Policy pages stay in the sitemap `loc` list (indexable in each language, just no
  alternate links).
- **DOM contract additions** (`docs/storefront-dom-contract.md`): `contact-form-submit` (new);
  `quote-form-submit` already exists.

---

## D2 — Authoring format: raw HTML `TextField`, rendered with `|safe` — matching the existing product-description decision

**Options considered:**
(a) raw HTML + `|safe` (the current product-description behavior);
(b) bleach/nh3-sanitized rich text; (c) markdown.

**Chosen: (a).**

**Why:** Consistency is the decision. Product and collection descriptions are **already** stored
as raw HTML and rendered with `|safe` (`product.html:141,145`, `collection.html:27`) — store
admins already possess exactly this injection capability platform-wide, plus
`announcement_text`. Introducing a different rule for static pages would create two content
models with different escaping semantics in the same template set (an invisible-failure factory)
without removing the existing exposure. (b) and (c) both require a new dependency (no sanitizer
or markdown library exists in `requirements.txt`) — dependency additions require human approval
and would have to be applied to product descriptions in the same stroke to be meaningful.

Threat framing: buyers never write into `body` — every buyer-supplied value (contact form fields,
search queries) renders through normal auto-escaping. `body` is written only by store admins with
the `pages` module permission; session cookies are host-scoped, and the admin surface is a
separate host from storefront domains.

**Standing obligation (unchanged from spec NFR-T3):** the platform-wide rich-text sanitizer
("`|safe` only on server-side-sanitized fields — sanitizer allowlist is the Safety Agent's to
approve") remains open and now covers **three** call sites: product description, collection
description, static page body. When it lands it must be ONE function
(`core/html_sanitize.py:sanitize_rich_text`) applied at the persistence boundary (§XV-5) to all
three — tracked as a named follow-up for the Safety Agent, not silently dropped.
**ASSUMPTION:** matching the existing precedent is acceptable until that platform-wide item is
scheduled; flagged to the Safety Agent gate for this ticket.

No WYSIWYG editor in v1 — a plain `<textarea>` in admin (admin UI polish is the Designer's lane).

---

## D3 — Store-level contact form: `ContactMessage` + honeypot + cache-based per-IP throttle

**Options considered for the model:** (a) reuse `QuotationRequest` with a `type` discriminator;
(b) dedicated `ContactMessage`. **Chosen: (b)** — different lifecycle vocabulary
(new/replied/closed vs pending/responded/closed), no quantity/product semantics, and a mixed
inbox would force nullable-field soup.

```python
# pages/models.py
class ContactMessageStatus(models.TextChoices):
    NEW = "new"; REPLIED = "replied"; CLOSED = "closed"

class ContactMessage(StoreOwnedModel):
    page      = models.ForeignKey(StaticPage, null=True, blank=True,
                                  on_delete=models.SET_NULL,
                                  related_name="contact_messages")  # provenance only
    name      = models.CharField(max_length=200, blank=True, default="")
    email     = models.EmailField()
    subject   = models.CharField(max_length=255, blank=True, default="")
    message   = models.TextField()
    lang_code = models.CharField(max_length=10, blank=True, default="")  # request.locale at
                                                                         # submit — reply language
    status    = models.CharField(choices=ContactMessageStatus.choices, default=NEW)
    created_at

    class Meta:
        indexes = [models.Index(fields=["store", "status"]),
                   models.Index(fields=["store", "created_at"])]
```

No IP address is persisted (data-minimal; throttling doesn't need storage).

### Anti-spam — `pages/antispam.py`, one helper, reused everywhere

No rate-limiting infra exists (verified: analytics ingest has none; no third-party throttle lib).
Build the minimal cache-based primitive — no new dependency:

```python
def rate_limit_exceeded(request, scope: str, *, limit: int = 5,
                        window_seconds: int = 3600) -> bool:
    """Cache-window counter keyed on (scope, store, client IP).
    cache.add(key, 0, window) then cache.incr(key) — atomic on Redis (production);
    LocMem in dev is per-process best-effort, which is acceptable for a spam gate."""

def honeypot_triggered(request, field: str = "website") -> bool:
    """True when the hidden field is non-empty. Caller returns the normal success
    response (silent drop + info log) — never reveal detection to the bot."""
```

- Client IP: `REMOTE_ADDR` (deployment must terminate proxies correctly — note for the release
  checklist; do not parse `X-Forwarded-For` in application code).
- Applied to: contact POST, store-level quotation POST (D4), **and retrofitted onto the two
  existing product form endpoints** (`notify_me_view`, `quotation_request_view` in
  `storefront/views_product_forms.py`) in the same ticket — one function, four call sites
  (§XV-4). Defaults: 5 submissions / IP / store / hour per form scope.
- On limit: re-render the page with a translated "too many requests" form error, HTTP 429
  (JSON `{"ok": false}` 429 for the AJAX path).
- Honeypot: hidden `website` input (CSS-hidden, `autocomplete="off"`, `tabindex="-1"`) in both
  form partials.

### Store notification + admin inbox

- On successful save, notify the store via
  `emails.service.send_transactional_email(template_id="contact_message_received", …)` — new
  file-based default template in `emails/templates/emails/`; the service's per-store override and
  never-raise semantics come for free. A failed email never fails the form response.
- **Recipients — PENDING APPROVAL (medium uncertainty).** Options: (1) all active
  `StoreEmployee` rows with `full_access=True`; (2) a new per-store "notification email" setting;
  (3) no email in v1, admin inbox only. **Recommended and to be implemented: (1)** — zero new
  settings surface, correct-by-construction recipients; (2) is a natural later refinement when
  the settings-validation area lands. Human may veto to (3) without schema impact.
- Admin: `ContactMessageAdmin` on `store_admin_site` following the `QuotationRequestAdmin`
  pattern (store-scoped queryset, no add) but with `status` **editable** (the inbox is a
  workflow, not a log): list `email, name, subject, status, created_at`, filter by status,
  module gate `_check_module_access(request, "pages", …)` — limited = read-only, full = status
  changes. **ASSUMPTION:** `"pages"` as the module key in the 18-module matrix (matrix vocabulary
  finalizes in TICKET-047; a one-word key rename is a trivial follow-up).

---

## D4 — Store-level quotation page: reuse `QuotationRequest` with a nullable product FK

**Options considered:**
(a) make `QuotationRequest.product` nullable — `NULL` = general (non-product) quotation;
(b) fold general quotations into `ContactMessage` with a subject preset;
(c) new `GeneralQuotationRequest` model.

**Chosen: (a).**

**Why:** The store admin needs ONE quotation inbox — B2B stores (the theme vertical that leans on
quotation flows, ADR-012 D11) will receive both product-anchored and general quote requests and
must triage them in one list with one status lifecycle. (b) buries sales leads inside a support
inbox with the wrong fields (no quantity) and wrong lifecycle; (c) is two models for one concept.
`product IS NULL` is an explicit, queryable discriminator — not state inferred from absence
(§XV-3): the row's meaning is complete, "general quotation" is what NULL *is*, and the admin list
shows it as "(general)".

Changes (all additive):

- Migration: `product` → `null=True, blank=True` on `catalog.QuotationRequest`
  (`on_delete=CASCADE` becomes `SET_NULL` so deleting a product keeps the lead). Existing rows
  untouched.
- The quotation static page (`kind=quotation`) POSTs `customer_name, email, message`
  (+ honeypot) to its own URL → creates `QuotationRequest(product=None, quantity=1)`.
- `QuotationRequestAdmin`: show `product` as "(general)" when NULL; make `status` editable under
  the same module gate as D3 (currently the admin is fully read-only, which contradicts the
  model's documented pending→responded→closed lifecycle — fix in this ticket).
- The existing product-anchored endpoint (`/products/<slug>/quotation/`) is untouched apart from
  the anti-spam retrofit (D3).

**Human decision 18:P2 (2026-07-10):** quantity is not displayed on any customer-facing quotation
form (store-level `request-a-quote` page and the product-anchored `/products/<slug>/quotation/`
form) — quotation-mode products cannot be added to the cart, so a requested quantity is not
meaningful at request time. Both view handlers now ignore any `quantity` POST value entirely; every
row is created with the model default (`quantity=1`). `QuotationRequest.quantity` itself is
retained (default 1) for potential admin-side use.

---

## D5 — Seeding: post-save signal on Store creation + idempotent management command; pages seeded as drafts

**Options considered:** (a) data migration; (b) management command only;
(c) `post_save` signal on `Store` (created=True) + idempotent management command for existing
stores.

**Chosen: (c).**

**Why:** A data migration runs once and covers only stores existing at migrate time — future
stores (the common case for a multi-tenant platform) get nothing, and page content doesn't belong
in migration history. A command alone relies on a human remembering to run it per store. The
signal covers every future store at the only creation point (super-admin `save_model` → `save()`);
the command (`manage.py seed_static_pages [--store <id>|--all]`, `get_or_create` on
`(store, slug)` — safe to re-run) backfills existing stores and repairs deletions.

Seed set (all **`is_published=False`** — drafts create no Permalink, so no thin/duplicate
placeholder content is ever crawlable; publishing is an explicit store-admin act):

| slug | title | kind | show_in_footer | show_in_header |
|---|---|---|---|---|
| `about-us` | About us | generic | yes | no |
| `contact` | Contact | contact | yes | yes |
| `request-a-quote` | Request a quote | quotation | yes | no |
| `shipping-policy` | Shipping policy | policy | yes | no |
| `terms-of-service` | Terms of service | policy | yes | no |
| `privacy-policy` | Privacy policy | policy | yes | no |
| `refund-policy` | Refund policy | policy | yes | no |

Titles/bodies seeded in the store's default language as short placeholder copy with a visible
"replace this text before publishing" marker. Terms/refund/privacy exist because payment
processor onboarding (Stripe) requires reachable URLs for them.

**Launch checklist integration point (named):** new launch-readiness item **L12 — "Legal pages
published"** (terms-of-service, privacy-policy, refund-policy each have `is_published=True` and
an active default-language Permalink). The launch-readiness engine itself is not yet built (only
L11 references exist in `storefront/theme.py`); L12 is recorded here and in
`06_settings_requirements.md` (Spec Agent follow-up) so the checklist ticket picks it up.

---

## D6 — Navigation: flag-driven footer/header link lists via one template tag; no menu builder

**Options considered:** (a) a full menu/MenuItem model (arbitrary trees, object refs);
(b) `show_in_header`/`show_in_footer` + `nav_position` flags on StaticPage rendered by a template
tag; (c) hardcoded policy links in the footer partial.

**Chosen: (b).** A menu builder is a separate future area (it must reference products and
collections too — when it comes, it stores object PKs per §III and supersedes these flags); (c)
violates the "every href through the resolver" rule (TH-042) and cannot reflect per-store pages.
Flags are the simplest viable thing and do not paint us into a corner.

```python
# pages/templatetags/pages_tags.py
@register.inclusion_tag("storefront/partials/static_page_links.html", takes_context=True)
def static_page_links(context, zone):   # zone in {"header", "footer"}
    ...
```

Rules (all enforced in the tag, not templates — §VI/§III):

- Emits only pages that are `is_published=True`, flagged for the zone, **and** have an active
  `Permalink` for `request.locale.language.lang_code` — Permalink presence is the routability
  test (ML-011); never render a link that would 404. Ordered by `nav_position, pk`.
- Label: published `StaticPageTranslation.title` for the lang; source `title` when the request
  lang is the store default. No cross-language fallback labels.
- Hrefs composed with the language prefix exactly as `_build_storefront_url` does (or reuse the
  existing `{% permalink_url %}` tag internals) — never a stored URL (§III).
- Two queries max, request-scoped cache on the request object (same pattern as
  `_seo_hreflang_cache` in `permalinks/hreflang.py`).
- Wired into the existing Phase-2 placeholder comments in
  `storefront/templates/storefront/partials/header.html` and `footer.html`. The partials remain
  engine-owned/locked (ADR-012 D3) — themes style them via tokens only.

---

## Implementation ticket (single Developer ticket — T029-SP "Static pages + store forms")

1. `pages` app: models (D1, D3), migrations (incl. `catalog.QuotationRequest.product` nullable —
   D4), admin (StaticPage CRUD + ContactMessage inbox + QuotationRequest status editing),
   `apps.py ready()` signal wiring.
2. `permalinks`: reserved top-level slugs; `handle_slug_change` StaticPage branch;
   `resolve_storefront_path` `staticpage` branch.
3. `storefront`: `views_pages.py` (GET render + kind-gated POST handlers, PRG),
   `pages/static_page.html` + `partials/contact_form.html` + `partials/static_quotation_form.html`
   (testids: `contact-form-submit`, `quote-form-submit`), DOM-contract doc update.
4. `pages/antispam.py` + retrofit onto `views_product_forms.py`.
5. Hreflang exemption in `permalinks_tags` + sitemap alternates suppression (D1 SEO).
6. Translation triggers (D1 §4) + `persist_output` support for `sub_type="static_page"` in the
   aijobs pipeline (writes `StaticPageTranslation`, same contract as product sub_type).
7. Seeding signal + management command (D5); nav tag + partial wiring (D6).
8. Email notification template + send call (D3).
9. Spec follow-ups (Spec Agent): TH-088 → DECIDED per this ADR; L12 in
   `06_settings_requirements.md`; `05_database_schema.md` gains the three tables + the
   QuotationRequest change.

Safety Agent gate required before release (public forms, raw-HTML content field, email fan-out).
SEO Agent review required (new indexable page type, hreflang exemption, sitemap change).

## Risks

- **Raw HTML body (D2)** — accepted consciously to match the product-description precedent;
  bounded by admin-only authorship; the platform sanitizer remains a named open item for the
  Safety Agent. Do not let this ADR be cited later as a reason to skip that item.
- **Root-level slug collisions with future fixed routes** — a new fixed URL added to
  `storefront/urls.py` shadows any same-named page. Mitigation: the reserved-slug set must be
  updated in the same PR as any new fixed route (comment at both sites + a test asserting the
  set covers every non-catch-all top-level pattern in `storefront/urls.py`, generated from
  `urlpatterns` so it cannot drift).
- **LocMem rate limiting is per-process** — under multi-worker dev it undercounts. Accepted:
  production uses Redis; the gate is best-effort spam control, not a security boundary.
- **Nav flags vs future menu builder** — flags become legacy when a menu system lands.
  Accepted: the tag is the single consumer; a menu system replaces the tag body, not templates.
- **Notification fan-out (D3)** — a store with many full-access employees gets one email each.
  Bounded (employee counts are small); superseded by option (2) if approved later.
- **Seeded drafts never published** — stores may launch without legal pages. Mitigated by L12;
  until the checklist engine exists, this is visible in admin (draft badge) only — known gap,
  inherited from the checklist area's schedule, not created here.

## Rollback strategy

- All schema changes are additive (three new tables; one FK made nullable — reverse migration
  restores `NOT NULL` only after deleting `product IS NULL` rows, which the reverse migration
  must do explicitly and loudly). No existing table is altered otherwise.
- Dispatcher/urls: the `staticpage` branch and view module are removable in isolation — products,
  collections, checkout are untouched; existing static-page Permalinks then resolve to None
  (404), which is the pre-ADR behavior.
- Seeding signal disconnect + command removal have no data consequences (drafts are inert).
- Anti-spam retrofit is a guard clause per view — removable line-by-line.
- Nav tag renders nothing if the `pages` app is removed (tag lives in the same app; partials
  degrade to the current empty placeholders).

## Tests required

- **Model/lifecycle:** publish → Permalink active (default lang); unpublish → deactivated → 404;
  translation publish/unpublish → translated Permalink lifecycle (AC-104 parity); slug change →
  SlugRedirect created, chain-collapse, loop rejection (StaticPage branch); reserved slug rejected
  (`cart`, `fr`, `collections`); reserved-set-covers-urlpatterns drift test; NFD slug from
  accented title (§II, via `make_slug`).
- **Dispatch/render:** `/about-us/` 200 with correct template + `content_object`; prefixed-lang
  path (`/fr/a-propos/`); non-default lang without published translation → 404; unpublished →
  404 real status (TH-087); POST to generic page → 405.
- **Forms (no-JS, Django test client):** contact + general quotation POST round-trip (PRG,
  row created, `lang_code` captured, store-scoped); honeypot filled → success response, no row;
  rate limit → 429 after N submissions, keyed per store and per IP; product-form endpoints
  inherit the throttle; CSRF enforced; email notification sent (and form succeeds when the email
  service fails); tenant isolation (store A page/messages never visible on/for store B).
- **D4:** general quotation row has `product=None`; product deletion keeps the row (SET_NULL);
  admin lists both kinds; status transitions permitted at "full", read-only at "limited";
  admin 403 without the module permission.
- **SEO:** policy page renders canonical but zero hreflang alternates; generic page renders
  full hreflang; sitemap contains the page, and policy-page sitemap entries have no alternates —
  parity test asserting sitemap alternates and page-level hreflang agree for both kinds
  (CAN-005); draft pages absent from sitemap.
- **Seeding:** new store gets exactly the 7 drafts; command idempotent (double run, no dupes);
  no Permalinks exist for seeded drafts.
- **Nav:** tag emits only published+flagged+permalink-active pages for the request lang;
  labels use translations; hrefs carry the lang prefix; ordering by `nav_position`; zone
  filtering; no link for a lang where the page translation is unpublished.

## Open items recorded in `11_uncertainties_to_validate.md`

- PENDING APPROVAL — D3 notification recipients (recommended: all full-access employees).
- ASSUMPTION — D2 raw-HTML body pending the platform-wide sanitizer (Safety Agent item, NFR-T3).
- ASSUMPTION — `"pages"` as the permission-matrix module key (TICKET-047 vocabulary).
