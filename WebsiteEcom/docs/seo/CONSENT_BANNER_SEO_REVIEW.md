# SEO Review — Consent Banner (TICKET-049, ADR-025 D5/D8 gate)

**Reviewer:** SEO Agent · **Date:** 2026-07-11 · **Scope:** review-only, no code changes.

**Files reviewed:**
- `docs/adr/ADR-025-consent-management.md` (D3 reload-on-grant, D5 banner/CLS claims)
- `storefront/templates/storefront/partials/consent_banner.html`
- `consent/slot_provider.py`, `consent/views.py`, `consent/urls.py`
- `storefront/templates/storefront/base.html` (`{% render_slot "consent" %}`, line 71) and
  `base_checkout.html` (inherits the slot unmodified, header comment line 10)
- `pixels/templates/pixels/ga_base.html` (Consent Mode v2 snippet)
- `pixels/slot_provider.py` (consent-gated render, `consent_mode` context)
- `sitemaps/views.py` (robots.txt Disallow list)
- `storefront/locale/fr/LC_MESSAGES/django.po` (banner string translations)

**Evidence run:**
`DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test consent storefront.tests.test_theme_conformance`
→ **139 tests, OK** (includes slot/theme conformance, prior-consent gating, Consent Mode v2
content assertions, i18n fr rendering, preview suppression).

---

## 1. CLS — ADR "zero CLS by construction" claim: CONFIRMED

- **Banner is out of flow.** `.consent-banner { position: fixed; left:0; right:0; bottom:0; }`
  (`consent_banner.html:109`). Fixed elements do not participate in document layout; they
  overlay content and contribute **zero** to Cumulative Layout Shift. Nothing pushes
  `<main>` (`base.html:59-61`) or the footer.
- **Hidden panel does not affect layout.** `.consent-panel` carries the `hidden` attribute
  (`consent_banner.html:54`) — `display: none`, no box, no layout contribution — and when
  opened is itself `position: fixed` (`consent_banner.html:134`), so opening it is also
  shift-free. Opening/closing is user input, which CLS excludes anyway (500 ms input window).
- **No late DOM injection.** The banner is server-rendered in the initial HTML response
  (`ConsentSlotProvider.render()` → `render_to_string`, `consent/slot_provider.py:122-133`),
  injected at `base.html:71`. The classic CMP CLS failure mode (async JS inserting a bar
  post-load) is structurally absent — there is no external script at all.
- **No font/image shift risk inside the banner.** `.consent-banner-root { font-family:
  inherit; }` (`consent_banner.html:107`) — no new web-font request; the banner contains no
  `<img>`, no icon font, no background image. Text renders with whatever font stack the page
  already resolved.
- **Worst-case unstyled-flash analysis** (the inline `<style>` at `consent_banner.html:106`
  comes *after* the banner markup in source order): even if a paint occurred between parsing
  the banner `<div>` and its `<style>`, the markup sits at the **end of `<body>`**
  (after `<main>` and the footer) — content appended at document end renders *below*
  existing content and moves nothing above it, so CLS is still zero. Optional polish, not
  required: emit the `<style>` before the markup inside the partial (F3 below).

**Verdict on the ADR claim:** zero CLS by construction — TRUE as designed. ✔

**Testable rule:** the banner root and panel must remain `position: fixed` (or `hidden`) and
must never gain `position: static/relative` — pin with an HTML assertion that the rendered
`<style>` block contains `position: fixed` for both `.consent-banner` and `.consent-panel`
(structural pinning, consistent with the no-JS-harness constraint of KNOWN_RISKS item 13).

## 2. Google intrusive-interstitial policy: PASS

- The banner is a bottom bar, `role="dialog" aria-modal="false"` (`consent_banner.html:35`)
  — non-blocking, content behind stays reachable and scrollable. No full-screen overlay on
  first paint, no scroll lock, no backdrop.
- **Viewport coverage bounded:** `max-height: 40vh; overflow-y: auto`
  (`consent_banner.html:110`). Realistic rendered height on a 360×640 mobile viewport
  (16 px title + 2–4 lines of 13 px text + three wrapped 41 px buttons + padding) is
  ≈ 200–260 px ≈ 31–40 % of viewport — under the cap, and the cap itself guarantees the
  main content always keeps ≥ 60 % of the viewport.
- Google's intrusive-interstitial guidance **explicitly exempts** "interstitials that appear
  to be in response to a legal obligation, such as for cookie usage" and "banners that use a
  reasonable amount of screen space". This banner qualifies on both grounds independently.
- The granular panel (`max-height: 80vh`, `consent_banner.html:135`) opens only on an
  explicit user action (Customize / footer button) — post-interaction UI is out of scope for
  the interstitial policy, which evaluates on-load and on-navigation state.

**Testable rule:** rendered banner CSS must keep a `max-height` ≤ 40vh; the panel must be
`hidden` in the initial response on every page type (already asserted by the conformance
suite — panel present, `hidden` attribute set).

## 3. Crawler behavior: PASS

- **Consentless render is complete and correct.** Googlebot sends no cookies →
  `get_consent()` = undecided → `show_banner=True` and `slot.pixels` renders nothing
  (`pixels/slot_provider.py:137`). Page **content is untouched** — gating affects only
  tracker snippets, which crawlers should not receive anyway. This is the correct
  crawl-friendly posture: faster pages for bots, no third-party JS in the crawl render.
- **No cloaking.** Bots and humans get byte-identical HTML for the same consent state; bots
  simply never consent. No user-agent branching anywhere in the consent path.
- **No indexing side effects.** `ConsentSlotProvider.render()` returns body-slot HTML only;
  it cannot touch `<head>` — no `noindex`, no robots meta, no canonical impact. Verified: no
  robots/meta output in the partial.
- **Payload per page is modest.** Rendered partial ≈ 9–10 KB uncompressed (markup ≈ 3.7 KB,
  CSS ≈ 2.3 KB, JS ≈ 4.3 KB; the 1.6 KB `{% comment %}` is stripped at render) ≈ 2.5–3 KB
  gzipped, on every page. Acceptable; repeated boilerplate is recognized and discounted by
  Google. See F2 for the one real residual risk (snippet pollution).
- **`/_consent/` endpoint:** POST-only (`@require_POST`, `consent/views.py:64`); GET → 405.
  The URL appears only inside the inline JS `fetch()` string, never in an `href`. Googlebot
  may still extract it from JS and probe it; a 405 is harmless and unindexable. Hygiene
  improvement F1 below.

## 4. Google Consent Mode v2 ordering: PASS

`pixels/templates/pixels/ga_base.html` — verified ordering within the inline script:

1. `gtag('js', new Date())` (line 5)
2. **`gtag('consent', 'default', {...})` (lines 14–19)**
3. `gtag('config', ...)` (line 27 or 29)

The consent default precedes the config call — Google's required ordering (the default must
be queued in `dataLayer` before any config/event push). The `async` gtag.js loader tag
(line 1) is irrelevant to ordering: the library replays the `dataLayer` queue in push order,
and the inline pushes execute synchronously during parse, before the async library can run.

Semantics are truthful per ADR-025 D3 §4: the snippet only exists when `analytics` is
granted (server-side gating), so `analytics_storage` is always `granted` here, and
`ad_storage`/`ad_user_data`/`ad_personalization` reflect the real `marketing` state
(`pixels/slot_provider.py:108-110` builds the flags from the single `get_consent()`
resolution — §XV-4 respected, no client-side re-derivation). All four v2 signals present.
Already pinned by tests (Consent Mode assertions in the suite run above).

## 5. Performance (inline CSS/JS on every page): PASS

- Injection point is the **end of `<body>`** (`base.html:71`, after `<main>` and footer) —
  parsing the banner cannot delay first paint of the main content; nothing here is
  render-blocking for content above it (no external stylesheet, no sync external script).
- ≈ 2.5–3 KB gzipped per page for markup+CSS+JS combined; zero extra HTTP requests (D5's
  "no extra render-blocking request" claim confirmed — everything is inline in the same
  response).
- The always-rendered hidden panel (~half the markup) on every page including post-decision
  pages is a deliberate ADR-025 D5 trade (footer "manage" reopens it without a round-trip);
  at this size it is the right trade.
- JS is a single IIFE with event listeners only — no work on the critical path, no layout
  reads, no polling.

## 6. Reload-on-grant (`location.reload()`): PASS — no SEO implication

- The reload fires only after a user click → POST → 204 (`consent_banner.html:212-219`).
  Crawlers never click, never POST, never reload — this code path is invisible to indexing.
- No redirect chain and no soft-404 risk: `location.reload()` re-requests the *same* URL,
  which returns its normal 200 with full content; there is no interstitial URL, no 3xx hop,
  no meta refresh. Query string (UTM) survival is an attribution concern only, already
  handled per ADR-025 D3.
- Refuse-all / no-new-grant saves do not reload (`consent_banner.html:214-218` else-branch
  closes the banner) — no gratuitous re-request even for users.

## 7. i18n / lang correctness: PASS

- Every visible string is `{% trans %}`/`{% blocktrans %}` (`consent_banner.html`
  throughout). French catalog verified in `storefront/locale/fr/LC_MESSAGES/django.po`:
  "We value your privacy" → "Nous respectons votre vie privée" (l.660), "Accept all" →
  "Tout accepter" (l.683), "Refuse all" → "Tout refuser" (l.687), "Cookie preferences" →
  "Préférences cookies" (l.697), and the `blocktrans` policy-link sentence with
  `%(policy_url)s` preserved (l.674-679). Pinned by the fr rendering test (suite run above).
- `render_to_string(..., request=request)` (`consent/slot_provider.py:122-133`) → the
  banner renders in the request's active language; it adds no `lang` attribute of its own,
  so it correctly inherits the page's `<html lang>` — banner language always equals page
  language (both derive from the same request locale). No hreflang interaction: the banner
  adds no links except the policy link, which resolves through the existing
  Permalink/ML-011 chain for the current language and is **omitted rather than broken**
  when no active permalink exists (`consent/slot_provider.py:45-75`) — no crawlable 404
  links introduced. ✔
- Policy link is a plain `<a href>` to a first-party StaticPage — correctly followable,
  no `rel` needed.

---

## Findings & fixes

| # | Severity | Finding | Fix | Status |
|---|---|---|---|---|
| F1 | Low (hygiene, non-blocking) | `sitemaps/views.py:129-135` robots.txt disallows `/cart/`, `/checkout/`, `/orders/`, `/search`, `/admin/`, `/superadmin/`, `/feeds/` but not the internal endpoints `/_consent/` (and `/_analytics/`). GET on them is 405 — harmless — but crawlers extracting the URL from inline JS will probe it and log crawl errors. | Add `"Disallow: /_consent/"` and `"Disallow: /_analytics/"` to the list in `sitemaps/views.py`. Test: robots.txt response contains both lines. | **APPLIED 2026-07-11** — both lines added in `sitemaps/views.py`; confirmed the real mount paths first (`webecom/urls.py`: `/_analytics/` → `analytics.urls`, `/_consent/` → `consent.urls`) — matches the review's assumption exactly, no path correction needed. Tests: `test_robots_txt_disallows_internal_consent_endpoint`, `test_robots_txt_disallows_internal_analytics_endpoint` (`sitemaps/tests/test_sitemaps.py`). |
| F2 | Low-medium (recommended, non-blocking) | Because the banner is **server-rendered raw HTML on every page** (unlike JS-injected CMPs, which crawlers often never render), Google can select banner text ("We value your privacy… You can accept, refuse, or customize.") as the SERP **snippet** for pages where it judges the meta description weak — a documented, common issue with server-rendered cookie notices. Google ships `data-nosnippet` precisely for this. | Add `data-nosnippet` to the `.consent-banner` div (`consent_banner.html:35`) and the `.consent-panel` div (`consent_banner.html:54`). Zero behavior change; guarantees consent copy never appears in search snippets. Test: rendered undecided-state HTML contains `data-nosnippet` on both testids. | **APPLIED 2026-07-11** — `data-nosnippet` added to both root divs. Tests: `test_banner_root_has_data_nosnippet`, `test_panel_root_has_data_nosnippet` (`consent/tests/test_slot_provider.py`). |
| F3 | Info (optional polish) | The inline `<style>` (`consent_banner.html:106`) appears after the banner markup. As analyzed in §1 this cannot cause CLS (end-of-body appends move nothing), but emitting the `<style>` first removes even the theoretical unstyled-flash frame on very slow devices. | Move the `<style>` block above the `.consent-banner-root` div in the partial. No test change needed. | **APPLIED 2026-07-11** — `<style>` block moved above the `.consent-banner-root` div in `storefront/templates/storefront/partials/consent_banner.html`. Cosmetic only; existing tests (`test_banner_style_block_uses_position_fixed`, `test_banner_uses_only_theme_tokens_for_color`) locate the block via `html.index("<style>")`, which is position-independent, so no test change was needed. |

All three fixes applied 2026-07-11 (developer agent). Full suite
(`consent sitemaps storefront`) re-run after the change: 642 tests, OK.

## ADR-025 D8 gate items — explicit confirmation

- **Zero CLS:** confirmed (§1) — fixed-position, server-rendered, no fonts/images, hidden
  panel is layout-inert.
- **No interstitial flagging:** confirmed (§2) — legally-obligated cookie banner + bounded
  ≤ 40vh bottom bar, both independently exempt under Google's guidance.
- **Crawl/index neutrality:** confirmed (§3) — full content for consentless requests, no
  cloaking, no head/meta impact, POST-only endpoint.

## Verdict

**PASS-WITH-FIXES** — both ADR-025 gate claims (zero CLS, no interstitial risk) are
confirmed against the implemented code; Consent Mode v2 ordering, crawler behavior,
performance, reload-on-grant, and fr i18n are all correct. Fixes F1–F3 are non-blocking
hygiene/hardening; F2 (`data-nosnippet`) is recommended before launch.
