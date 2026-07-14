# ADR-021: Translation activation for the storefront (gettext catalog wiring)

**Status: ACCEPTED (human, 2026-07-10) — recommended defaults throughout;
no blocking uncertainty. Low-uncertainty items are marked ASSUMPTION per the uncertainty
policy. Supersedes nothing; extends ADR-008 (locale resolution) and ADR-015 §6 (checkout
language).**

## Decision

1. **`stores.middleware.LocaleMiddleware` becomes the single activation point** for
   Django's translation machinery on storefront requests: after `resolve_locale()`
   succeeds it calls `django.utils.translation.activate(locale.language.lang_code)` and
   sets `request.LANGUAGE_CODE`. On **every** other code path (excluded prefixes, and
   before `resolve_locale` runs) it activates `settings.LANGUAGE_CODE` so no request ever
   inherits the previous request's thread-local language.
2. **Scope is storefront-only.** `/admin/`, `/superadmin/` and the other excluded prefixes
   keep the platform default language (ASSUMPTION — no spec item requires a translated
   admin chrome; see §Scope).
3. **No `Vary: Accept-Language` anywhere on the storefront.** Language is fully determined
   by host + path (ADR-008); responses never depend on the `Accept-Language` header. The
   middleware sets `Content-Language` on storefront responses. The country-meta endpoint's
   existing `Vary: Accept-Language` is **removed** and replaced by an explicit `lang`
   query parameter (see §Country-meta).
4. **`sf_lang` is NOT dead — it is the checkout-language carrier and stays.** Checkout
   views additionally activate `resolve_checkout_language(request)` (session
   `sf_lang`-first, per ADR-015 §6 ML-001) via a small decorator, because on
   path-prefixed languages checkout runs on the *un-prefixed* `/checkout/…` URLs where
   `resolve_locale` returns the domain's root language.
5. **Missing-catalog fallback = Django default (silent msgid/English).** Acceptable for
   v1; a launch-readiness warning is a non-blocking follow-up.
6. One small Developer ticket: middleware activation + checkout decorator + country-meta
   `lang` param + tests. No schema change, no new dependency, no settings change beyond
   comments.

## Context

A complete French UI catalog exists and compiles (`storefront/locale/fr/LC_MESSAGES/django.mo`,
`locale/fr/LC_MESSAGES/django.mo`, `LOCALE_PATHS` configured, `USE_I18N = True`), and
storefront templates are fully `{% trans %}`-tagged. But nothing in the request path ever
calls `translation.activate()`:

- `stores/middleware.py::LocaleMiddleware` resolves host + path → `RequestLocale`
  (ADR-008 §2a), sets `request.locale` / `request.store`, and writes
  `session['sf_lang']` — and stops there.
- Django's own `django.middleware.locale.LocaleMiddleware` is **intentionally absent**
  (`webecom/settings/base.py` i18n comment block): storefront languages are dynamic
  per-store rows (`StoreLanguage.lang_code`), there is no static `LANGUAGES` tuple, and
  Accept-Language negotiation is explicitly not wanted — the URL is the sole source of
  language truth.
- Result: every production storefront request renders gettext strings in the thread's
  default language (English msgids). The French catalog only works under explicit
  `translation.override()` — which is exactly what the test suite does, so tests pass
  while production is monolingual. This is a textbook Lesson-1 invisible failure
  (`design-pattern-ideas.txt` §XV-1): HTTP 200, no exception, wrong output.

Relevant current facts (verified in code):

- `MIDDLEWARE` order (`webecom/settings/base.py:49`): Security → **Session** →
  FirstTouchUTM → Common → HostResolution (legacy) → **stores.LocaleMiddleware** →
  ThemeMiddleware → Csrf → Auth → Messages → XFrameOptions. `SessionMiddleware` already
  runs before `LocaleMiddleware` (required by the existing `sf_lang` write), and
  `ThemeMiddleware` reads only `request.store`, never the translation state — **no
  reordering is needed**.
- `sf_lang` **is consumed**: `storefront/views_checkout.py::resolve_checkout_language`
  (priority: `session['sf_lang']` → `request.locale.language.lang_code` → `'en'`), which
  feeds `Order.checkout_language` (orders/models.py:166, U16-8 email-language snapshot).
  It is not dead and must not be removed.
- Path-prefix stripping happens **inside `resolve_locale` only** (`RequestLocale.path`);
  `request.path_info` is never rewritten and URLconf routing is prefix-blind. Fixed
  routes (`/checkout/…`, `/cart/`, `/search/`) therefore only match on their bare,
  un-prefixed form; `/fr/checkout/` would fall into the `<path:slug>` catch-all. This is
  precisely why ADR-015 introduced `sf_lang`: the buyer browses under `/fr/…`, then
  checkout happens at bare `/checkout/` where `resolve_locale` yields the **root**
  language. Any activation design must honor that split.
- `checkout.js:291` fetches the country-meta endpoint with an **absolute** URL
  (`/checkout/address/country-meta/?country=XX`), so on path-prefixed languages the
  request carries no language signal at all. The endpoint currently sets
  `Cache-Control: public, max-age=86400` + `Vary: Accept-Language`
  (views_checkout.py:338-339) — a header that varies on a request dimension the server
  never reads.
- There are no `cache_page` decorators, no other manual `Vary` headers, and no
  per-view response caching in storefront production code today. No CDN contract is
  encoded in the codebase beyond the country-meta headers.
- No production code calls `translation.activate/override` anywhere (only tests do).

## Options considered

### D1 — where to activate

1. **Activate inside `stores.middleware.LocaleMiddleware` (chosen).** The middleware
   already owns resolution (Lesson 4: shared resources need a single resolution
   function — and a single *activation* point is the same principle). Zero new
   components, correct ordering for free.
2. A per-view decorator on every storefront view. Rejected: N call sites, guaranteed
   drift — the next view forgets it and silently renders English (Lesson 1). Views
   reached via the permalink catch-all would each need it.
3. Django's `django.middleware.locale.LocaleMiddleware` with a custom
   `get_language_from_request`. Rejected: that middleware is built around
   `settings.LANGUAGES` / `get_supported_language_variant` / Accept-Language and
   cookie negotiation, plus `i18n_patterns` redirects — all explicitly rejected by
   ADR-008's dynamic per-store model. Bending it costs more than the ~10 lines it saves,
   and it would patch `Vary: Accept-Language` onto responses, which is wrong here (§D3).

### D1a — response-phase handling

1. **Deterministic activation at the start of every request; no `deactivate()` (chosen).**
   Same strategy as Django core's LocaleMiddleware: never deactivate, always activate.
2. `translation.deactivate()` in the response phase. Rejected: (a) unnecessary once every
   request activates deterministically first; (b) actively dangerous with
   `TemplateResponse` — deferred rendering happens *after* middleware response phases, so
   deactivating there would render a lazily-rendered response in the wrong language.
   Today's storefront views use `render()` (pre-rendered), but the design must not break
   the day someone returns a `TemplateResponse`.

### D2 — scope

1. **Storefront paths only (chosen).** Excluded prefixes get `settings.LANGUAGE_CODE`.
2. Also translate `/admin/` per the logged-in store admin's preference. Rejected for now:
   no spec item requires it (checked `02_feature_matrix.md`, `07_multilingual_seo.md`,
   `10_implementation_tickets.md` — every ML-* item is about *content* translation and
   the admin screens that manage it, not the admin chrome language), there is no admin
   locale catalog, and there is no per-user language field. **ASSUMPTION:** store-admin
   UI stays default-language; revisit only if a spec adds it.

### D3 — cache/header correctness

1. **No `Vary: Accept-Language`; set `Content-Language`; make country-meta language
   explicit in the URL (chosen).**
2. Keep/add `Vary: Accept-Language`. Rejected: the server never reads Accept-Language,
   so the header is semantically false — it needlessly fragments shared caches by a
   request header that does not influence the response, and on country-meta it *fails*
   to capture the dimensions that will actually influence the response (host, and after
   this ADR the `lang` param).
3. `Cache-Control: private` on country-meta to allow session-driven language. Rejected:
   kills the 24h shared cacheability of a hot, tiny endpoint for no reason once the
   language is in the cache key (query param).

### D4 — `sf_lang`

1. **Keep it; checkout views activate via `resolve_checkout_language` (chosen).**
2. Remove it and rely on `request.locale`. Rejected: provably wrong on path-prefixed
   languages, where checkout URLs are un-prefixed and `request.locale` is the root
   language (see Context). `sf_lang` is the only carrier of the buyer's browsing
   language across that boundary, and `Order.checkout_language` depends on it.
3. Rewrite `request.path_info` in the middleware (strip the prefix, like `i18n_patterns`)
   so `/fr/checkout/` routes natively and `request.locale` is always right. Genuinely
   attractive, but it is a **routing-model change to ADR-008**, touches permalink
   resolution, canonical URLs and every fixed route, and far exceeds "one small ticket".
   Rejected here; noted as a possible future ADR if prefixed-language checkout UX (URLs
   staying under `/fr/`) is ever required.

## Chosen option

D1-1 + D1a-1 + D2-1 + D3-1 + D4-1. Concretely (design sketch, not production code):

```python
# stores/middleware.py  — LocaleMiddleware.__call__ (delta only)
from django.conf import settings
from django.utils import translation

def __call__(self, request):
    # Deterministic baseline on EVERY request — kills thread-local leakage from
    # the previous request served by this worker thread, and guarantees a defined
    # language for 404 pages raised by resolve_locale itself (unknown host).
    translation.activate(settings.LANGUAGE_CODE)
    request.LANGUAGE_CODE = translation.get_language()

    ... excluded-prefix passthrough, bare-prefix 301, resolve_locale, 410 ...

    # After successful resolution (existing request.locale / request.store /
    # sf_lang logic unchanged):
    translation.activate(locale.language.lang_code)
    request.LANGUAGE_CODE = translation.get_language()

    response = self._get_response(request)
    # Read get_language() (not the resolved code) so a checkout-view activation
    # of sf_lang is reflected. setdefault: never clobber a view-set header.
    response.headers.setdefault("Content-Language", translation.get_language())
    return response
```

```python
# storefront/views_checkout.py — decorator applied to the buyer-facing checkout
# views (checkout_view, contact/address/shipping/pay POSTs, payment_return,
# checkout_retry, thank-you):
def activate_checkout_language(view):
    # translation.activate(resolve_checkout_language(request)); no restore needed —
    # the middleware re-activates deterministically on every request.
    ...
```

```python
# country_meta_view — language becomes part of the URL (cache key):
#   GET /checkout/address/country-meta/?country=US&lang=fr
# - lang param: optional; sanitized (lowercase, ^[a-z]{2}(-[a-z]{2})?$ — same shape
#   ADR-008 uses for prefixes); invalid/absent → resolve_checkout_language(request)
#   for backward compatibility, but checkout.js is updated to always send it
#   (from document.documentElement.lang, which base.html already sets from
#   request.locale).
# - translation.override(lang) around label serialization.
# - Cache-Control: public, max-age=86400 kept; Vary: Accept-Language REMOVED.
```

`activate()` cost is negligible: a thread-local write plus a per-process-cached
`DjangoTranslation` lookup.

## Why

- **Single activation point mirrors the single resolution point** (ADR-008's own design
  rule and Lesson 4). The middleware is the only place that knows the resolved
  `StoreLanguage`; putting activation anywhere else re-derives it.
- **Baseline-activate-always is the only leak-proof shape** for a middleware with
  excluded prefixes. Without it, a worker thread that just served `/fr/robe-rouge/`
  would render the next `/admin/` request — which bypasses this middleware — in French.
  Django core avoids this by activating on *every* request; we must too, explicitly,
  because of our passthrough branch.
- **No ordering hazards**: Session already precedes Locale (existing `sf_lang`
  dependency); Theme follows and is translation-agnostic; CSRF/Auth/Messages are
  language-independent. The early-return branches (bare-prefix 301, 410) carry no
  translatable body. No middleware move required.
- **Vary reasoning confirmed**: with language-in-URL (dedicated domain or path prefix),
  every language variant has a distinct URL, so URL-keyed caches (CDN, shared proxies)
  are already correct without any `Vary`. `Vary: Accept-Language` would claim a
  dependency that does not exist. The one endpoint that *did* have a hidden extra
  dimension (country-meta: same bare URL across a domain's prefixed languages) is fixed
  by promoting that dimension into the URL — the cache-correct move — rather than into
  headers or session.
- **Checkout split respected**: ADR-015 §6 already defines the checkout language as
  `sf_lang`-first; this ADR wires that existing decision into gettext instead of
  inventing a second mechanism. `Content-Language` follows the *actually active*
  language via `get_language()` at response time.

## Fallback semantics (D5)

`translation.activate('de')` for a `StoreLanguage` with no compiled catalog raises
nothing: Django builds an empty translation object and every `gettext` call falls
through to the msgid — i.e. the English source string — silently. **Accepted for v1**:
it degrades to exactly today's behavior, per-language and per-string, and matches
ADR-014's stance that translation coverage is tracked, not hard-blocked.
`lang_code` values come only from `StoreLanguage` rows (resolve_locale matches actual
rows, never a bare regex — ADR-008 §2a), so `activate()` never receives attacker- or
typo-controlled input on the middleware path; the country-meta `lang` param is regex-
sanitized before use.

**Non-blocking follow-up (Lesson 1, recorded — not in this ticket):** add a
launch-readiness / settings-validation *warning* ("UI catalog missing for enabled
language X — storefront chrome will show English") by checking for a compiled
`django.mo` per enabled `StoreLanguage`. Warn, don't block: stores may legitimately
launch a language whose *content* is translated while chrome strings lag.

Out of scope, unchanged by this ADR: order/campaign **email** rendering language
(`Order.checkout_language` snapshot exists but has no consumer yet — owned by
ADR-015/U16-8 follow-up work), and AiJob-driven *content* translation (ADR-014).

## Risks

- **Thread-local leakage if the baseline activation is skipped** (e.g. a future early
  return added above it). Mitigated: the baseline is the first statement of
  `__call__`, and a dedicated leak test locks it in (T4 below).
- **Checkout language ≠ URL language on prefixed stores** is now user-visible (chrome
  in `sf_lang`, URL un-prefixed). This is ADR-015's accepted model, not a regression;
  the D4-3 path-rewrite option is the eventual clean fix if ever needed.
- **country-meta backward compatibility**: cached 86400s responses without the `lang`
  param persist up to a day after deploy; they contain default-language labels — same
  as today, so no worse than the status quo during rollout.
- **Third-party/template code calling `get_language()`** now sees per-request values
  instead of a constant. Audited: no production call sites exist today; templates read
  `request.locale.language.lang_code` directly (base.html:2). Behavior change is the
  *intended* one.
- **`LANGUAGE_CODE = "en-us"`** becomes the visible language of excluded paths and
  pre-resolution error pages — identical to current behavior, just now explicit.

## Rollback strategy

Single revert of the ticket's commit restores today's behavior exactly: no schema
change, no data migration, no settings change, no new dependency. The `lang` param on
country-meta is additive (absent param falls back), so JS and view can be reverted
independently without breaking either direction. Feature-flagging is not warranted for
a change this size; the revert IS the rollback plan.

## Tests required

- **T1 (the headline integration test)**: create a store with a French `StoreLanguage`
  (dedicated-domain case) + a published French permalink; issue
  `client.get(path, HTTP_HOST=fr_host)` with **no `translation.override` anywhere in the
  test**; assert a known French catalog string (a `{% trans %}` msgstr from
  `storefront/locale/fr`) appears in the body and `Content-Language: fr` is set.
- **T2**: same for the path-prefix case (`/fr/...` on a shared domain).
- **T3**: default/root-language request renders English and `Content-Language` is the
  root language.
- **T4 (leak test)**: request a French storefront page, then an excluded-prefix path
  (`/admin/...` or `/static/...`) with the same client/thread; assert the second
  response contains no French catalog strings / `translation.get_language()` baseline
  is the default. Also: unknown-host 404 page renders default language.
- **T5 (checkout `sf_lang`)**: browse a `/fr/`-prefixed page (sets `sf_lang='fr'`),
  then GET bare `/checkout/`; assert French checkout chrome despite
  `request.locale` being the root language.
- **T6 (country-meta)**: `?country=DE&lang=fr` returns French labels with
  `Cache-Control: public, max-age=86400` and **no** `Vary: Accept-Language`; invalid
  `lang` (`?lang=%0A`, `?lang=zz-zz-zz`) falls back safely (200, default-language
  labels, no error); missing `lang` keeps working (backward compat).
- **T7 (fallback)**: enabled language with no catalog (e.g. `de`) renders English
  msgids with `Content-Language: de` and no exception.
- **T8 (regression guard)**: existing ADR-008 resolution tests (bare-prefix 301, 410
  disabled language, unknown host 404) still pass unchanged — activation must not
  alter any resolution outcome.

## Implementation ticket sketch (one small ticket)

1. `stores/middleware.py`: baseline + resolved activation, `request.LANGUAGE_CODE`,
   response-phase `Content-Language` (setdefault, from `get_language()`). Update module
   docstring.
2. `storefront/views_checkout.py`: `activate_checkout_language` decorator on buyer-facing
   checkout views; country-meta `lang` param + `override()` + remove
   `Vary: Accept-Language` (update its docstring, which currently cites that header).
3. `storefront/static/storefront/js/checkout.js`: append `&lang=` from
   `document.documentElement.lang` to the country-meta fetch.
4. Tests T1–T8.
5. `webecom/settings/base.py`: extend the i18n comment block with one line pointing at
   this ADR as the activation design.
