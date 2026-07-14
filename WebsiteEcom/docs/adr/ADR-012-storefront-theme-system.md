# ADR-012: Storefront theme system — shared DOM contract, token-skinned themes, slot registry

**Status:** ACCEPTED (2026-07-05) — implements TICKET-029. Ratifies the human-approved
decisions of 2026-07-05 (3 themes B2B/Fashion/General, no visual builder, **Django templates (DTL)**,
plain CSS custom properties + vanilla JS, single-page checkout, signed preview tokens, numbered
pagination, speed-first). These decisions are frozen; this ADR designs their architecture.

**Extends:** ADR-001 §3b (platform-owned Theme, existing `stores.Theme` stub + `Store.theme` FK),
ADR-005 (PermalinkResolver), ADR-008 (`resolve_locale`, `StoreDomain`/`StoreLanguage`, `base_url`),
ADR-011 (thank-you tokens, capture window).
**Spec:** `specs/ecommerce_engine/15_storefront_theme_system.md` (TH-xxx requirements; §3.6 vertical
for Theme A superseded — see Consequences).
**Binding constraints:** design-pattern-ideas §III (single URL resolver), §VI (media fallback in model
helpers, never templates), §X (inline beacon, FiredPixel dedup), §XI (`seo_head` in the base template,
one source), §XV-1 (no invisible failures), §XV-4 (single resolution function), §XV-5 (validation at
persistence boundaries).

---

## Context

- The storefront does not exist yet: `webecom/urls.py` mounts admin, webhooks, beacon, sitemaps,
  campaign upsell endpoints and the checkout-resume link, but no buyer-facing pages.
- Infrastructure that T029 must consume, already live:
  - `stores.LocaleMiddleware` calls `resolve_locale(host, path)` once per request and sets
    `request.locale` (`RequestLocale(store, domain, language, path)` — path has the language prefix
    stripped) and `request.store` (ADR-008 §2a).
  - `permalinks.resolver.resolve(store, lang, slug)` → `ResolvedPage | ResolvedRedirect | None`,
    and `base_url(language)` — the only URL composition point (ADR-005/008).
  - `{% seo_head content_object %}` inclusion tag (`permalinks/templatetags/permalinks_tags.py`)
    already emits canonical + hreflang + og:locale; T025 extends this tag, not templates.
  - Analytics beacon endpoint `/_analytics/` (T012); `orders.FiredPixel` dedup table.
  - Upsell endpoints `/campaigns/upsell/accept/` + `/decline/` (T028/ADR-011), token-guarded
    thank-you semantics, `capture_window_expires_at`.
  - `stores.Theme` is a stub (`name`, `is_active`) with `Store.theme` FK (`SET_NULL`, nullable).
- The central spec constraint (TH-040/TH-046): **all three themes share ONE template set and ONE DOM
  contract**. A theme is a skin — design tokens + curated assets + a small whitelist of overridable
  partials — never a divergent template tree. Functional tests run once against the shared contract.
- `TEMPLATES` uses DTL with `APP_DIRS=True`; the approved engine decision (DTL) means **zero new
  template dependencies** and supersedes spec TH-001's Jinja2 recommendation.
- Approved theme verticals: **B2B** (professional/clean), **Fashion** (editorial/stylish),
  **General** (all-purpose, reference + platform default). This supersedes spec §3.6's Foods
  vertical for Theme A; structural contracts of §3.6 (card ratios, header variants, hover-swap)
  otherwise carry over with B2B replacing "Marché".

---

## Decisions

### D1 — New `storefront` Django app owns the engine; themes are non-app packages under `themes/`

**Options considered:** (a) storefront views/templates spread across existing apps (catalog renders
product, cart renders cart…); (b) one `storefront` app owning views, URL conf, base templates, theme
loader, slot registry — themes as data-only packages; (c) each theme as a Django app.

**Chosen: (b).** One app = one place for the shared template set (the DOM contract is a single
artifact, reviewable as one tree), one URL conf appended last, one theme-resolution code path.
(a) scatters the contract across apps and makes the "themes never fork templates" rule unenforceable;
(c) makes themes installable code with app-registry side effects — themes must stay inert data
(templates + CSS + JSON), reviewable as diffs, incapable of registering models or signals.

**On-disk layout:**

```
storefront/                     # Django app (engine)
  urls.py, views/, middleware.py, theme.py, slots.py, context_processors.py
  templates/storefront/
    base.html
    pages/{home,collection,product,cart,checkout,thankyou,search,404,static_page}.html
    partials/{header,footer,product_card,price,gallery,cta_block,pagination,...}.html
    slots/            # empty placeholder markup for unregistered slots
  static/storefront/  # core.css (structural, token-consuming), core.js (vanilla ES modules)
themes/                         # NOT a Django app — inert theme packages
  b2b/
    tokens.json
    templates/b2b/partials/     # whitelisted override partials ONLY (D3)
    static/b2b/                 # theme.css, fonts (WOFF2), curated imagery
  fashion/  (same shape)
  general/  (same shape — reference theme, platform default)
```

Settings wiring (no custom loader — stock Django mechanisms only):
- `TEMPLATES[0]["DIRS"] += [BASE_DIR / "themes" / k / "templates" for k in discovered_themes()]`
  — each theme namespaces its templates under `templates/<key>/…` (standard app-template
  convention), so names never collide across themes.
- `STATICFILES_DIRS += [BASE_DIR / "themes" / k / "static" for k in ...]` — assets addressed as
  `<key>/theme.css`; `ManifestStaticFilesStorage` gives hashed immutable filenames (TH-061).

`tokens.json` (validated by a Django **system check** at startup — missing/invalid file fails
deployment loudly, §XV-1):

```json
{
  "version": 1,
  "key": "b2b",
  "card_aspect_ratio": "4:5",
  "tokens":     {"--p-color-brand": "#1f3a5f", "--p-color-accent": "…", "--p-font-heading": "…",
                 "--p-font-body": "…", "--p-radius": "2px", "…": "…"},
  "presets":    {"navy":  {"--p-color-brand": "…", "…": "…"},
                 "slate": {"…": "…"}},
  "font_pairs": {"grotesk": {"--p-font-heading": "…", "--p-font-body": "…", "files": ["…"]}},
  "structural_defaults": {"sticky_header": true, "announcement_bar": false,
                          "breadcrumbs": true, "hover_second_image": false}
}
```

### D2 — Theme selection: extend the existing `stores.Theme` row + new `StoreThemeCustomization`

**Options considered:** (a) StoreSetting key/value entries; (b) dedicated models — extend the
`Theme` stub (platform registry row per theme package) + a per-store customization table.

**Chosen: (b).** Theme activation needs FK integrity (`Store.theme` already exists), a platform
default flag with a DB constraint, and per-(store, theme) customization that survives switching
back (TH-021) — none of which a settings blob gives. Migration is **additive** on the stub:

```python
class ThemeEngine(models.TextChoices):
    CODE = "code", "Code theme (DTL)"          # renames spec's 'twig_code' — DTL decided
    VISUAL_BUILDER = "visual_builder", "Visual builder"   # P3, no P1 rows

class Theme(models.Model):                      # existing stub, fields added
    name = models.CharField(max_length=255)
    engine = models.CharField(max_length=20, choices=ThemeEngine.choices,
                              default=ThemeEngine.CODE)
    source_ref = models.CharField(               # directory key under themes/
        max_length=64, unique=True,
        help_text="Theme package directory key, e.g. 'b2b' → themes/b2b/.")
    is_default = models.BooleanField(default=False)   # platform default (blue checkmark)
    status = models.CharField(choices=[("draft", …), ("published", …)], default="draft")
    is_active = models.BooleanField(default=True)
    thumbnail = <media ref, nullable>            # library card image
    created_at / updated_at

    class Meta:
        constraints = [models.UniqueConstraint(
            fields=["is_default"], condition=Q(is_default=True),
            name="one_default_theme")]
```

- **Theme defaults live in `tokens.json`, not in a DB column.** Deviation from 05_database_schema's
  hypothesized `Theme.customization_json` `[H]`: defaults must version with the CSS that consumes
  them — a DB copy drifts against deployed code (invisible-failure class). The DB row is a registry
  pointer; `source_ref` binds it to the package. `is_backup_copy` is dropped (theme duplication is
  P3, out of scope).
- No `organization` FK, no nullable-store scoping games — library rows are platform-global
  (ADR-001 §3b, TH-003). Store admins get read-only listing + an activation action writing
  `Store.theme` only.

```python
class StoreThemeCustomization(StoreOwnedModel):
    """Per-store customization of one theme (TH-021). Switching themes and back
    restores this record — it is never deleted on switch."""
    theme = models.ForeignKey("stores.Theme", on_delete=models.CASCADE,
                              related_name="customizations")
    customization_json = models.JSONField(default=dict)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        constraints = [models.UniqueConstraint(fields=["store", "theme"],
                                               name="uniq_customization_per_store_theme")]
```

`customization_json` schema (validated at the persistence boundary, §XV-5 — one
`validate_customization(theme, data)` function used by admin form AND model `clean()`):
`preset` (must be a key in the theme's `presets`), `font_pair` (ditto), `color_overrides`
(hex-validated, **whitelisted slot names only** — `primary, accent, background, surface, text,
muted_text, badge, announcement`; unknown keys rejected), `logo`/`favicon`/`hero_images`
(media refs via T003 pipeline; per-language hero variants resolved by the TranslatedMedia-style
model helper, never in templates — §VI), `announcement_text`, `structural` (boolean toggles from
TH-020.5). WCAG-AA contrast below 4.5:1 → blocking admin warning requiring explicit
"save anyway" (TH-023).

### D3 — Shared template set; theme overrides limited to a hard whitelist, resolved by one tag

**Options considered:** (a) custom template `Loader` aware of the active theme (thread-local
request state); (b) an explicit `{% theme_partial %}` tag using `select_template` fallback;
(c) themes may override any template.

**Chosen: (b).** (a) hides request state in a loader — magic, untestable ordering, and it silently
widens what a theme can override. (c) violates the frozen "one template set" constraint. The tag is
explicit, DTL-native, and enforces the whitelist at render time:

```python
THEME_OVERRIDABLE_PARTIALS = frozenset({
    "hero",           # home hero composition
    "theme_band",     # home theme-specific band (B2B: benefits row; Fashion: lookbook strip;
                      #                            General: trust/benefits row)
    "product_badge",  # sale/new badge treatment on product cards
    "footer_band",    # decorative footer strip above the functional footer
})

@register.simple_tag(takes_context=True)
def theme_partial(context, name):   # {% theme_partial "hero" %}
    if name not in THEME_OVERRIDABLE_PARTIALS:
        raise TemplateSyntaxError(...)          # loud, not fallback (§XV-1)
    key = context["theme_ctx"].key
    tmpl = select_template([f"{key}/partials/{name}.html",
                            f"storefront/partials/{name}.html"])
    return tmpl.render(context.flatten())
```

**Locked (never theme-overridable):** `base.html`, every `pages/*.html`, and every functional
partial — price display, CTA block (all inventory-mode states), gallery, variant selector, forms,
checkout sections, funnel widget, pagination, header/footer *structure*. Header layout variance
(Fashion's centered logo, B2B/General left logo) is a **token/CSS-class variant**
(`--p-header-layout` consumed by `core.css`), not a template fork. A CI conformance check walks
`themes/*/templates/*/partials/` and fails on any file outside the whitelist.

**Template inheritance and block structure** (three levels: base → page → *nothing* — themes do
not extend templates):

```django
{# storefront/templates/storefront/base.html — engine-owned, locked #}
<html lang="{{ request.locale.language.lang_code }}">
<head>
  {% block seo_head %}{% seo_head content_object %}{% endblock %}   {# §XI — D6 #}
  <style>{{ theme_ctx.token_css }}</style>          {# resolved tokens, D4 #}
  <link rel="stylesheet" href="{% static 'storefront/core.css' %}">
  <link rel="stylesheet" href="{% static theme_ctx.css_path %}">    {# <key>/theme.css #}
</head>
<body data-theme="{{ theme_ctx.key }}">
  {% render_slot "consent" %}
  {% include "storefront/partials/announcement.html" %}   {# gated by structural toggle #}
  {% include "storefront/partials/header.html" %}         {# menus via PermalinkResolver tag #}
  {% block breadcrumbs %}{% include "storefront/partials/breadcrumbs.html" %}{% endblock %}
  <main id="main">{% block content %}{% endblock %}</main>
  {% include "storefront/partials/footer.html" %}          {# incl. language switcher #}
  {% render_slot "overlay" %}{% render_slot "social_proof" %}{% render_slot "pixels" %}
  <script nonce="{{ request.csp_nonce }}">
    {% analytics_beacon %}                                  {# D7 — inline, first-party #}
  </script>
  <script type="module" src="{% static 'storefront/core.js' %}"></script>
</body></html>
```

Page templates (`pages/product.html` etc.) `{% extends "storefront/base.html" %}`, fill
`{% block content %}`, may *append* to `seo_head` via `{{ block.super }}` + the T025 tag API only
(e.g. product OG image, JSON-LD) — they never emit canonical/hreflang/robots themselves (TH-041).
`pages/checkout.html` and `pages/thankyou.html` override `breadcrumbs` empty and render a reduced
header (distraction-free), still within the same base.

**URL emission rule (TH-042):** every internal href in any template goes through the
PermalinkResolver template tag (menus reference objects by PK, resolved for
`request.locale.language` at render time — §III). CI lint greps `storefront/templates` +
`themes/` for hardcoded `href="/` outside the tag and fails the build.

### D4 — Theme + token resolution: one function each, request-scoped, cached

```python
# storefront/theme.py — the ONLY code that decides which theme renders (§XV-4)
@dataclass(frozen=True)
class ThemeContext:
    theme: Theme; key: str; tokens: dict; token_css: str
    customization: dict; structural: dict; card_aspect_ratio: str
    css_path: str; is_preview: bool

def resolve_theme(store, preview: PreviewGrant | None = None) -> ThemeContext: ...
def resolve_tokens(theme_key: str, customization: dict) -> dict: ...
```

- **Fallback chain (TH-008):** `preview.theme` (if valid grant) → `store.theme`
  (active+published) → platform default (`is_default=True`) → `settings.STOREFRONT_REFERENCE_THEME`
  (`"general"`, package existence asserted by the system check). Any fallback past `store.theme`
  logs at error level and is surfaced by launch-readiness check **L11 "Active theme selected"**
  (settings-validation architecture) — rendering never 500s for a missing theme row, and never
  falls back silently.
- **Token resolution order (TH-022):** per-slot `color_overrides` → selected `preset`/`font_pair`
  → `tokens.json` defaults. Output is restricted to the whitelisted `--p-*` names; unknown keys are
  dropped + logged. Serialized once into `token_css` (`:root{--p-…}` block) and **inlined in
  `<head>`** — one cache read, no extra request, no FOUC (TH-024). Cache key:
  `(store_id, theme_id, customization.updated_at, tokens.json["version"])`; invalidated by save
  (key changes) and by theme switch in the same transaction (TH-006).
- A lightweight `ThemeMiddleware` (after `LocaleMiddleware`, storefront paths only) sets
  `request.theme_ctx`; a context processor exposes `theme_ctx` to templates. Views never resolve
  themes themselves.
- Theme structural CSS (`themes/<key>/static/<key>/theme.css` + `storefront/core.css`) contains
  **only `var(--p-*)` references**, never store-specific literals — enforced by review + a CI grep
  for hex literals in theme CSS outside `tokens.json`.

### D5 — URL mounting: catch-all `storefront` URL conf, dispatched through the permalink resolver

**Options considered:** (a) rewrite `request.path_info` in middleware to the prefix-stripped path;
(b) duplicate every URL pattern with an optional `<lang>/` prefix; (c) explicit fixed routes + one
catch-all dispatcher that routes `request.locale.path` through the permalinks resolver.

**Chosen: (c).** (a) corrupts Django's redirect/reverse machinery; (b) is N patterns × N languages
of drift. (c) keeps path→page decomposition in exactly one place, symmetric with `base_url`'s
composition (§XV-4):

```python
# webecom/urls.py — appended LAST (catch-all must not shadow admin/webhooks/sitemaps)
path("", include("storefront.urls")),

# storefront/urls.py
urlpatterns = [
    path("cart/", cart_view),                    # un-prefixed, localized strings (07 §2)
    path("checkout/", checkout_view),
    path("search/", search_view),
    path("orders/<str:public_order_id>/thank-you/", thankyou_view),
    re_path(r"^.*$", content_dispatch),          # home, collections, products, static pages
]
```

`content_dispatch` reads `request.locale.path` (already prefix-stripped by `resolve_locale` —
themes/views never re-derive language, §III) and calls a new
`permalinks.resolver.resolve_storefront_path(locale) -> ResolvedHome | ResolvedPage |
ResolvedRedirect | None` helper — the single place that decomposes `/collections/<slug>/` vs
`/<slug>/` vs `/`, mirroring `base_url`'s composition and reusing the existing
`resolve(store, lang, slug)`. Outcomes: `ResolvedPage` → render the page template for its content
type; `ResolvedRedirect` → 301/302/410 per the redirect table; `None` → the 404 page with a **real
HTTP 404** (TH-087, never soft-200). Only `published` translations resolve (ML-011 — already
enforced inside the resolver). Numbered pagination on collections/search (`?page=N`, approved) with
`rel=prev/next` handled by the T025 tag API.

Checkout view integration: payment container only — the routed `ProcessorAccount` comes from the
T020 decision-tree service; card fields are processor-hosted elements; the theme styles the
container. Coupon and gift-card inputs are two distinct fields wired to the existing discounts
service (atomic redemption stays server-side, §XIII). Single-page checkout with grouped sections
(contact → shipping → payment on one scroll), all sections plain `<form>` POST-capable — JS is
progressive enhancement (TH-063).

### D6 — SEO head: the existing `{% seo_head %}` tag, in the base template, once

Already implemented the right way (§XI): `{% block seo_head %}` lives in `base.html` with the
`permalinks` app's `{% seo_head content_object %}` inclusion tag as its default body (canonical +
hreflang + og:locale today; T025 extends the *tag and its included partials* with meta
title/description, robots, structured data). Themes cannot touch it (base is locked); page
templates extend it only via `{{ block.super }}` + T025 API. Policy pages get T025's legal
hreflang no-op through the same tag, not a template fork. Preview mode injects
`<meta name="robots" content="noindex">` through this block's context flag (D8) — one emission
point for robots directives.

### D7 — Analytics beacon: inline via an `analytics`-owned template tag; pixels via the slot registry

The beacon (T012 event set: page_view, product_view, collection_impression, scroll_depth,
variant_select, add_to_cart) is **inlined** in the base template's single first-party `<script>`
block (§X — no separate file, no defer-missed events), CSP-nonce'd (NFR-T3). Decoupling:
the JS source and event wiring live in the **analytics app** as `{% analytics_beacon %}`
(`analytics/templatetags/`); the storefront base template includes the tag once; **themes contain
no script tags at all** (CI grep). The tag reads `request.theme_ctx.is_preview` and renders nothing
in preview — suppression cannot be forgotten per-page. Purchase-grade events are NOT the beacon's
job: purchase pixels key off payment confirmation with `FiredPixel` insert-before-fire dedup
(T030's contract, §X).

### D8 — Preview mode: signed grant, cookie-carried, marketing-suppressed

- **Issue:** store-admin theme screen (gated by the "Themes: Full access" permission row) POSTs to
  `storefront:preview_start`; server issues `django.core.signing.dumps({"store_id", "theme_id",
  "customization_id" | None}, salt="theme-preview")`. TTL enforced via `max_age` on verification —
  `settings.STOREFRONT_PREVIEW_TTL = 2h` default, hard-capped ≤ 24 h (TH-007). The grant never
  mutates `Store.theme`.
- **Enter:** any storefront URL + `?theme_preview=<token>`. `ThemeMiddleware` verifies (signature,
  TTL, `store_id == request.store.id` — a token for store A is invalid on store B), then sets a
  `__theme_preview` cookie (HttpOnly, Secure, SameSite=Lax, max-age = remaining TTL) so navigation
  stays in preview; each request re-verifies the cookie value. Invalid/expired token → normal
  rendering (log at info; the admin screen shows "preview expired").
- **Preview responses:** `X-Robots-Tag: noindex, nofollow` header + meta robots via the `seo_head`
  context flag; `Cache-Control: no-store`; beacon renders nothing (D7); slot categories `pixels`,
  `overlay`, `social_proof` render empty (D9); a fixed "Preview: <theme name> — not live" bar with
  an exit link (clears the cookie). Checkout under preview is allowed to render but the payment
  section shows a static disabled notice — no processor calls from preview sessions.

### D9 — Component API contract: versioned `data-testid` doc + slot registry (plugin pattern)

- **DOM contract:** `docs/storefront-dom-contract.md` v1, versioned in-repo; changing any testid
  requires updating the shared functional test suite in the same change (TH-046). Initial set:
  `add-to-cart`, `preorder-cta`, `soldout-badge`, `notify-me-email`, `quote-form-submit`,
  `variant-selector`, `qty-stepper`, `product-card`, `product-price`, `pagination`, `sort-select`,
  `search-input`, `cart-open`, `cart-line-remove`, `coupon-input`, `gift-card-input`,
  `checkout-submit`, `funnel-accept`, `funnel-decline`, `funnel-countdown`, `language-switcher`,
  `announcement-bar`, `newsletter-email`. Identical across themes — a theme switch can never break
  a functional test, only visuals (US-T5).
- **Slot registry** (`storefront/slots.py`) — the mandated registry/plugin pattern for regularly
  added/removed storefront integrations:

```python
class SlotProvider:            # connector interface
    slot: str                  # e.g. "pixels"
    key: str; name: str
    required_settings: list[str]
    def is_enabled(self, store) -> bool: ...
    def render(self, context) -> str: ...       # returns safe HTML
    def validate_settings(self, store) -> list[str]: ...   # feeds launch checklist

register(provider)             # module-level registration at app ready()
```

  `{% render_slot "pixels" %}` iterates providers registered for the slot, renders enabled ones,
  and always emits the stable wrapper `<div data-slot="pixels"></div>` (hook point present even
  when empty — T035's popup JS and tests can target it unconditionally). Declared P1 slots:
  `consent` (gates `pixels`, TH-141 — slot only), `pixels` (T030), `overlay` (T034),
  `social_proof` (T035), `security_badge`, `bump_product` / `bump_cart` / `bump_floating_cart`
  (T027 placements), `thankyou_funnel` (T028 — the engine ships this provider itself, D10),
  `amazon_buy` (T036), `chat_launcher` (P3, reserved), `reviews` (T023 — product-page review
  section + card stars mount here until T023 lands). T029 ships the registry + empty wrappers;
  integrating tickets ship providers. A theme that fails to render a declared slot fails the
  conformance suite (TH-045).
- Required settings of registered providers plug into the settings-validation architecture: a
  provider that `is_enabled` but fails `validate_settings` appears on the launch checklist —
  missing pixel IDs are visible in admin, never silently unrendered (§XV-1).

### D10 — Thank-you page + upsell widget

`thankyou_view` is token-guarded per ADR-011 (`thankyou-view` token; invalid/expired → 404, never
an order-ID-guessable page), `noindex`. It renders the order recap (provisional `PENDING_CAPTURE`
funnel items excluded from the recap totals) and the `thankyou_funnel` slot provider, which:
renders the current step's offer (translated content from `CampaignStepTranslation`, `published`
only), a visible countdown to `capture_window_expires_at`, and **POST-only** accept/decline forms
targeting the existing `/campaigns/upsell/accept/` + `/decline/` endpoints with the single-use
`upsell-act` token (CSRF on, testids `funnel-accept`/`funnel-decline`). After window expiry or a
terminal session state the provider renders nothing — state read server-side at render, the
countdown JS only hides the widget client-side (server remains authoritative). Issued
storewide-discount codes (ADR-009 Q3) display in the recap. No purchase pixel fires here on
reload — `FiredPixel` dedup is checked by the T030 provider, not by the template (§X).

### D11 — The 3 theme packages

`b2b`, `fashion`, `general` under `themes/`, each: `tokens.json` (3–4 palette presets, 2–3 font
pairs, card ratio, structural defaults), `theme.css` (token-consuming skin over `core.css`),
self-hosted subsetted WOFF2 fonts (`font-display: swap`, no Google Fonts CDN — TH-062), curated
imagery defaults, override partials within the D3 whitelist only. Structural deltas: **General** —
reference theme + platform default, 4:5 cards, left-logo header, strongest social-proof treatment;
**Fashion** — 3:4 cards, centered-logo header token, hover-second-image default on, monochrome +
single accent; **B2B** — 1:1 or 4:5 cards (Designer's call), clean/professional, quotation-form
prominence (Branch D CTA emphasis), restrained palette. Exact hexes/typefaces/imagery remain
**PENDING (11:#11 — Designer proposals)**; engine + `general` proceed now (TICKET-029's own note).
Performance budgets are per-theme CI gates: theme CSS ≤ 60 KB gz, first-party JS ≤ 50 KB gz, ≤ 2
font files above the fold, zero render-blocking third-party requests (NFR-T1).

---

## Implementation plan (phased — each phase independently mergeable and testable)

### Phase 1 — Engine wiring: theme selection, mounting, base skeleton
1. `stores` migration: extend `Theme` (engine, source_ref, is_default + constraint, status,
   thumbnail), add `StoreThemeCustomization`. Data migration seeds the `general` Theme row
   (`status=draft`, `is_default=True`).
2. New `storefront` app: `ThemeMiddleware` + `resolve_theme`/`resolve_tokens` +
   `ThemeContext`, context processor, system check (tokens.json presence/validity, override
   whitelist, `STOREFRONT_REFERENCE_THEME` package exists).
3. `base.html` + header/footer/announcement partials + `{% theme_partial %}` + slot registry with
   empty wrappers; `{% analytics_beacon %}` tag in the analytics app.
4. URL mounting: `storefront.urls` appended last; `resolve_storefront_path` helper in
   `permalinks/resolver.py`; `content_dispatch` with home/404 pages (real 404).
5. Admin: super-admin theme library CRUD + default flag; store-admin read-only library +
   activate action + customization form (`validate_customization`, contrast warn-and-confirm);
   launch-readiness item L11.
6. CI: hardcoded-href lint, theme-dir script-tag lint, override-whitelist check, size-budget stub.
   **Tests:** theme fallback chain (incl. error logging on missing rows); token resolution order +
   whitelist filtering + cache invalidation on save/switch; customization validation (bad hex,
   unknown slot, foreign preset key rejected); store admin cannot mutate Theme rows (HTTP 403
   test); dispatcher: prefixed + un-prefixed paths, redirect passthrough, 404 status, published-only.

### Phase 2 — Commerce pages
1. `pages/collection.html` (grid, numbered pagination, sort, empty state), `pages/product.html`
   (gallery, variant selector, price partial with taxes-included note, all CTA inventory-mode
   states, description tabs per feature toggles — disabled toggle removes the element from the
   DOM), `pages/search.html`, `pages/static_page.html` (+ contact/quotation forms, no-JS POST).
2. `pages/cart.html` + floating mini-cart; `pages/checkout.html` single-page grouped sections,
   T020-routed payment container, distinct coupon/gift-card inputs, shipping methods (T032).
3. `core.js` progressive enhancements (gallery swipe, mini-cart, qty stepper, beacon events).
   **Tests:** every CTA state renders per inventory mode; feature toggles remove DOM nodes;
   all forms function without JS (Django test client, no headless browser needed); pagination
   crawlability (plain hrefs); permission-free public access + tenant isolation (store A page
   never renders store B data); DOM contract testids present on every page (contract test).

### Phase 3 — Thank-you + funnel embedding
1. `thankyou_view` (token-guarded, noindex) + recap; `thankyou_funnel` slot provider (offer,
   translated content, countdown, POST accept/decline against existing T028 endpoints).
   **Tests:** invalid/expired token 404; provisional items excluded from recap; widget absent
   after expiry/terminal state; accept/decline POST round-trip against the real service;
   reload fires no duplicate purchase event (FiredPixel assertion with a stub provider).

### Phase 4 — The 3 theme token bundles
1. `general` completed as reference theme (tokens, theme.css, fonts, assets); mark `published`.
2. `b2b` + `fashion` packages once Designer proposals approved (11:#11) — structure can land with
   placeholder tokens behind `status=draft`.
3. Theme conformance suite (TH-132): parametrized over theme keys — all pages render, all slots
   emitted, full DOM contract present, size budgets pass, tokens.json schema valid.
   **Tests:** the conformance suite itself, run per theme in CI.

### Phase 5 — Preview mode + SEO hardening
1. Preview grant issue endpoint (permission-gated), token/cookie verification in
   `ThemeMiddleware`, suppression behaviors (robots header+meta, no-store, beacon/pixel/overlay
   suppression, preview bar, disabled payment section).
2. `seo_head` extension points verified with T025 (block.super discipline test); hreflang/canonical
   parity across themes (byte-identical head SEO for the same URL under different themes).
   **Tests:** preview token TTL/store-binding/tamper rejection; noindex + no-store headers;
   beacon and marketing slots empty in preview; preview never mutates `Store.theme`; exit clears
   cookie; SEO-head byte-parity across the 3 themes.

Suggested tickets: T029-P1 … T029-P5 mirroring the phases (to be appended to
`specs/ecommerce_engine/10_implementation_tickets.md` on acceptance). Safety review checkpoints:
Phase 2 (checkout, public forms) and Phase 5 (signed tokens) are mandatory Safety Agent gates.

---

## Risks

- **Whitelist erosion** — pressure to let themes override "just one more" partial recreates three
  codebases. Mitigation: whitelist is a frozen constant + CI check; widening it requires an ADR
  amendment, not a PR.
- **Token sprawl** — unbounded `--p-*` names make theme CSS unreviewable. Mitigation: token name
  whitelist in the resolver; unknown names dropped + logged; additions reviewed with the Designer.
- **Catch-all URL pattern shadowing** — a future app mounted after `storefront.urls` is
  unreachable. Mitigation: comment at the mount point + a test asserting known non-storefront
  routes (admin, webhooks, sitemaps, campaigns) still resolve.
- **Inline token `<style>` vs CSP** — requires a nonce or `style-src` allowance. Mitigation:
  nonce the style block alongside the script nonce (NFR-T3); Safety Agent to approve the CSP
  policy in Phase 2 review.
- **Preview cookie leakage into caches** — mitigated by `Cache-Control: no-store` on every
  preview response and cookie-scoped verification per request.
- **`tokens.json` ↔ Theme-row drift** — a row whose `source_ref` has no package (or vice versa)
  renders fallback. Mitigation: system check at startup + launch checklist L11; never silent.
- **Designer-pending visuals** — b2b/fashion blocked on 11:#11. Mitigation: `status=draft` rows
  are not activatable by stores; engine + general are unblocked.
- **Performance budget regressions** — enforced as CI failures, not review notes; Lighthouse in
  the release gate (NFR-T1).

## Rollback

- All schema changes are additive (`Theme` new columns with defaults, one new table). Rollback =
  reverse migration; the pre-existing stub (`name`, `is_active`) and `Store.theme` FK remain valid.
- The storefront mount is one line in `webecom/urls.py`; removing it restores today's
  admin/webhooks-only surface with zero effect on existing apps.
- Theme packages are inert directories; deleting one only affects stores pointing at it, which the
  fallback chain + L11 check make visible, not fatal.
- Preview mode is independently removable (middleware branch + endpoint); tokens simply stop
  verifying.
- Per-store rollback of a bad theme choice = re-activate the previous theme (customization records
  are retained per (store, theme), so switching back restores the prior state — TH-021/TH-006).

## Tests required

Aggregated headline list (details per phase above): theme/token resolution + fallback + caching;
customization persistence-boundary validation + contrast gate; activation permissions (store admin
403 on Theme mutation; employee "Themes" matrix row); dispatcher routing/redirect/404/published-only
+ prefixed-language paths; DOM-contract presence test parametrized over themes; theme conformance
suite (pages × slots × budgets); no-JS form flows; thank-you token guard + funnel window behavior;
preview token security (TTL, tamper, cross-store) + suppression behaviors; SEO-head byte-parity
across themes; CI lints (hardcoded hrefs, theme script tags, override whitelist, size budgets).

## Consequences / spec follow-ups (Spec Agent)

- `15_storefront_theme_system.md`: mark TH-001 DECIDED = **DTL** (supersedes Jinja2
  recommendation; "Twig-style" requirement satisfied by curly-brace DTL syntax + the locked
  template set); TH-060 DECIDED = plain CSS + vanilla JS; TH-084 DECIDED = single-page checkout;
  TH-007 DECIDED = signed preview; TH-081 DECIDED = numbered pagination. **§3.6 Theme A vertical
  superseded: B2B replaces Foods/"Marché"** (approved 2026-07-05); Designer proposals (11:#11) to
  target B2B/Fashion/General.
- `05_database_schema.md`: replace the Theme `[H]` row with D2's shape (defaults in `tokens.json`,
  not `customization_json`; `is_backup_copy` dropped; add `StoreThemeCustomization`).
- `06_settings_requirements.md`: L11 wording + slot-provider `validate_settings` feeding the
  launch checklist.
- Email templates keep their own engine decision independent (the "same engine family" argument
  for Jinja2 is void now that themes are DTL — flag for the emails area owner).
