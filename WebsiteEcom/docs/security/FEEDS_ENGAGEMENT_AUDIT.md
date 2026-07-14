# Security audit — Catalog feeds (TICKET-033 / ADR-026) + Conversion overlays (TICKET-034/035 / ADR-027)

**Auditor:** Safety Agent (pre-release gate named in both ADRs)
**Date:** 2026-07-11
**Scope:** `feeds/` and `engagement/` apps in full, plus their storefront, emails,
permalinks, discounts, customers and antispam touch points.
**Evidence:** `DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test feeds engagement`
→ **242 tests, all pass** (3.4 s). Non-ASCII `hmac.compare_digest` behaviour reproduced
independently in a Python shell.

---

## Re-verification and FINAL VERDICT — Safety Agent, 2026-07-11

All six findings were reported fixed by the developer. The Safety Agent
**independently re-verified each fix against the current code** (reading the
implementations and their regression tests, not the fix descriptions) and
re-ran the suites: `DJANGO_SETTINGS_MODULE=webecom.settings.test python3
manage.py test feeds engagement` → **270 tests, all pass** (was 242 at the
original audit; the delta includes the F1/F3/F4/F6 regression tests plus
spec-review-fix tests landed since).

### Per-fix verification

- **F3 — VERIFIED FIXED.** `FEEDS_ROOT` (`webecom/settings/base.py:362`,
  default `BASE_DIR/feeds_files`, env-overridable) is used by
  `feeds/tasks.py::_feed_file_path:102`; `feeds_files/` is gitignored
  (`.gitignore:9`). The original exposure reasoning was re-run against the new
  path: `FEEDS_ROOT` is referenced **nowhere** in any URLconf or
  static-serving helper (grep across `webecom/` and `feeds/` confirms only
  tasks/views-via-DB/tests/settings); the DEBUG `static()` helper at
  `webecom/urls.py:75` maps only `MEDIA_URL→MEDIA_ROOT`; the production
  `/media/` reverse-proxy mapping therefore no longer covers feed files. The
  token-checking `feed_view` + `FileResponse` is now the only serving route.
  Regression test
  `feeds/tests/test_tasks.py::test_generated_file_is_never_written_under_media_root`
  pins path-not-under-`MEDIA_ROOT` and path-under-`FEEDS_ROOT`.
  **Residual (deploy checklist, not code):** (1) the production curl check
  stands — verify `/media/feeds/...` returns 404 AND that no nginx
  `alias`/`root` maps `BASE_DIR` broadly (which would re-expose
  `feeds_files/`); (2) if any pre-fix environment ever generated files under
  `MEDIA_ROOT/feeds/`, delete that directory (the feature is unreleased, so
  production has none); (3) an operator overriding the `FEEDS_ROOT` env var
  must choose a non-web-served path — note this in the deployment docs.
- **F1 — VERIFIED FIXED.** Both sides byte-encoded at `feeds/views.py:61-63`;
  regression test `test_non_ascii_token_returns_403_not_500`
  (`feeds/tests/test_views.py:61`) passes.
- **F2 — VERIFIED FIXED.** `StoreFeedToken.rotate()` logs store id + timestamp
  (`feeds/models.py:183`) with a do-not-remove docstring; the admin
  `rotate_token_view` additionally logs the acting user (`feeds/admin.py`);
  no token value is ever logged; the misleading receiver is renamed
  `_feed_token_created` with a corrected docstring (`feeds/signals.py:130`).
- **F4 — VERIFIED FIXED, and the 400-over-404 deviation is CORRECT.**
  `engagement/views.py:263-265` rejects a cross-store presentation with the
  **same status and body** as a bad/expired token. A 404 here would have
  created a third distinguishable response class — an oracle revealing "this
  token is cryptographically valid and its signup exists, just on another
  store" — exactly what this audit's no-oracle requirement forbids. The
  developer was right to follow the audit wording. Test:
  `test_confirm_token_from_another_store_returns_400_and_does_not_confirm`
  (`engagement/tests/test_views.py:394`) also asserts no state change.
  The `store is not None` guard is acceptable (host resolution always sets
  the store on real requests; the pattern mirrors every other view here).
- **F5 — VERIFIED FIXED.** Honeypot branch returns the full success key set
  (`ok/action/message_html/redirect_url/code`) with placeholder values and no
  DB reads (`engagement/views.py:117-133`). Residual (INFO, accepted): a bot
  that already knows a campaign's real success text could still distinguish
  by *values*; achieving value parity would require DB lookups in the
  honeypot path, defeating its purpose. Key-set parity is the right balance.
- **F6 — VERIFIED FIXED.** `_json_script_escape()`
  (`engagement/slot_providers.py:32-52`) escapes `&`, `<`, `>` as `&`/
  `<`/`>` (a superset of the recommendation, matching Django's
  `json_script` convention) and is applied to **both** `config_json` (line
  123) and `entries_json` (line 165). Hostile-slug tests exist for both
  payloads (`engagement/tests/test_slot_providers.py:181,374`) using a
  literal `</script><script>alert(1)</script>` slug.

### Spec-review deltas since the original audit (security relevance)

- `is_de_targeting_store` widened to language-OR-ships-to-DE
  (`engagement/service.py:185-210`) — over-inclusion is the safe direction
  for a consent gate; strictly an improvement.
- `LeadCaptureCampaign.clean()` now requires
  `reward_discount_code.provenance == LEAD_CAPTURE` and the admin FK queryset
  filters to the same (`engagement/models.py:205`, `engagement/admin.py:154`)
  — hardening: prevents accidentally linking a high-value generic code.
- `UniqueConstraint(fields=["store"])` added on `SocialProofSettings`
  (migration 0002) — closes the duplicate-singleton race.
- Overlay theme preview gallery (`engagement/widgets.py`,
  `engagement/static/engagement/previews/*.svg`): presentation-only
  `RadioSelect` widget; values still validated by the registry-derived
  `ChoiceField` + `clean()`; previews are code-defined static SVGs; no user
  input rendered. No new security surface.
- `pa:cart-busy` emitter and the feed price guard are behavioral fixes with
  no new trust boundary. Nothing in this batch reopens a reviewed area.

### Final verdict table (supersedes the original table below)

| # | Area | Verdict (2026-07-11, post-fix) |
|---|------|-------------------------------|
| 1 | Feeds — token scheme | CLEAR |
| 2 | Feeds — file serving | **CLEAR-WITH-NOTES** (was BLOCKED — F3 fixed in code; deploy-time curl verification remains a release-checklist item) |
| 3 | Feeds — data exposure | CLEAR |
| 4 | Feeds — generation tasks | CLEAR-WITH-NOTES (unchanged accepted notes) |
| 5 | Engagement — lead signup POST | CLEAR (F5 value-parity residual accepted as INFO) |
| 6 | Engagement — reward issuance | CLEAR |
| 7 | Engagement — social proof data | CLEAR |
| 8 | Engagement — overlay JS | CLEAR |
| 9 | Engagement — email confirm flow | CLEAR-WITH-NOTES (token-in-access-logs inherent to the pattern, accepted) |

**FINAL VERDICT: APPROVED WITH CONDITIONS** — all code-level findings are
fixed and independently verified; the feeds BLOCKED verdict is lifted. The
sole remaining conditions are **deploy-time checklist items** for the Release
Manager (no code changes required):

1. On the production VPS, verify
   `curl -s -o /dev/null -w '%{http_code}' https://<store-domain>/media/feeds/1/google/us-en.xml`
   returns 404, the tokened `/feeds/...` URL returns 200, and no nginx
   `alias`/`root` directive maps `BASE_DIR` (which would expose `feeds_files/`).
2. Document in the deployment notes that a `FEEDS_ROOT` env override must
   point to a non-web-served path.
3. Delete any stale `MEDIA_ROOT/feeds/` directory from pre-fix environments.

---

## Verdict summary (original audit — superseded by the re-verification above)

| # | Area | Verdict |
|---|------|---------|
| 1 | Feeds — token scheme | CLEAR-WITH-NOTES (F1, F2) — **APPLIED 2026-07-11** |
| 2 | Feeds — file serving | **BLOCKED** (F3 — token gate bypassable via `/media/feeds/…`) — **APPLIED 2026-07-11** |
| 3 | Feeds — data exposure | CLEAR |
| 4 | Feeds — generation tasks | CLEAR-WITH-NOTES |
| 5 | Engagement — lead signup POST | CLEAR-WITH-NOTES (F4, F5) — **APPLIED 2026-07-11** |
| 6 | Engagement — reward issuance | CLEAR |
| 7 | Engagement — social proof data | CLEAR |
| 8 | Engagement — overlay JS | CLEAR-WITH-NOTES (F6) — **APPLIED 2026-07-11** |
| 9 | Engagement — email confirm flow | CLEAR-WITH-NOTES |

**Overall: APPROVED WITH CONDITIONS.** One blocking condition (F3) on the feeds
ticket; the engagement ticket has no blocking finding. F1/F2/F4/F6 are recommended
fixes, non-blocking. Do not release T033 until F3 is resolved (code fix) or the
deployment mitigation is applied, verified, and added to the release checklist.

**Remediation status (2026-07-11):** all six findings (F1–F6) below are
**APPLIED** — see each finding's own status line for the code change and its
regression test. `DJANGO_SETTINGS_MODULE=webecom.settings.test python3
manage.py test feeds engagement` → **249 tests, all pass** (was 242; +7 new
regression tests). Full suite (`manage.py test`, no args) → **3019 tests, all
pass**. F3's human curl-verification checklist item (production VPS) is still
outstanding — that step requires a deployed environment and is out of scope
for this code-fix pass; it belongs on the release checklist.

---

## Findings

### F3 — HIGH / BLOCKING — Feed files are reachable under `/media/feeds/…` without any token

**STATUS: APPLIED (2026-07-11).** Option (a) implemented: `FEEDS_ROOT`
(`webecom/settings/base.py`, default `BASE_DIR / "feeds_files"`,
env-overridable) is a directory never wired into any URLconf or static-serving
helper. `feeds/tasks.py::_feed_file_path` now writes under `FEEDS_ROOT`
instead of `MEDIA_ROOT/feeds/`; `feeds/views.py` needed no change (`FileResponse`
already serves from any filesystem path). `feeds_files/` added to `.gitignore`
(and `media_files/`, a pre-existing gap in the same pattern). Regression test:
`feeds/tests/test_tasks.py::ProcessTargetTest::test_generated_file_is_never_written_under_media_root`
asserts the generated path is not under `MEDIA_ROOT` and is under `FEEDS_ROOT`.
ADR-026 and the `FeedTarget.file_path` help text updated to match. **Still
outstanding:** the human curl-verification checklist item
(`curl .../media/feeds/1/google/us-en.xml` → 403/404 on the production VPS)
requires a deployed environment — add it to the T033 release checklist.

- Feed files are written to `MEDIA_ROOT/feeds/<store_id>/<provider>/<country>-<lang>.xml`
  (`feeds/tasks.py::_feed_file_path`).
- `webecom/urls.py:75` serves the whole of `MEDIA_ROOT` at `MEDIA_URL='/media/'` when
  `DEBUG=True` — in development the token check is bypassable today at
  `/media/feeds/1/google/us-en.xml`.
- In production, S3 storage is **commented out** (`webecom/settings/production.py:112-118`)
  and product images live under the same `MEDIA_ROOT` with `MEDIA_URL='/media/'` — so the
  reverse proxy **must** map `/media/` to `MEDIA_ROOT` for the storefront to work at all.
  That same mapping exposes every feed file with **no token, from any host** (static file
  serving ignores the Host→store resolution), for **any store** (store ids are small
  sequential integers; provider/country/lang segments are trivially enumerable).
- Consequences: AC-213's token requirement (the anti-bulk-scraping control) is
  decorative; token rotation revokes nothing on this path; cross-tenant fetching is
  possible. Data severity is limited (feed bodies are public storefront data by design,
  see area 3), but the feature's own stated security control is nullified, and
  `robots.txt` disallows `/feeds/` but not `/media/feeds/`.

**Fix (pick one, prefer a):**
- (a) Write feed files to a dedicated directory that is never web-served, e.g.
  `FEEDS_ROOT = BASE_DIR / "feeds_files"` (settings-overridable), used by
  `_feed_file_path()`; `FileResponse` serves from any filesystem path, so the view
  needs no change. One-line migration concern: existing `FeedTarget.file_path` values
  regenerate on the next beat tick anyway (mark all dirty on deploy).
- (b) Keep `MEDIA_ROOT/feeds/` but ship a documented, mandatory
  `location /media/feeds/ { deny all; }` (nginx) rule, add it to the release checklist
  and to a deployment-hardening doc, and add a human verification step (curl the
  `/media/feeds/...` path on the VPS, expect 403/404).

**Human checklist item (either fix):** on the production VPS, verify
`curl -s -o /dev/null -w '%{http_code}' https://<store-domain>/media/feeds/1/google/us-en.xml`
returns 403/404, and that the tokened `/feeds/...` URL still returns 200.

### F1 — LOW — Non-ASCII `?token=` crashes `feed_view` with a 500

**STATUS: APPLIED (2026-07-11).** `feeds/views.py::feed_view` now compares
`provided_token.encode("utf-8")` against `store_token.token.encode("utf-8")`.
Regression test: `feeds/tests/test_views.py::FeedViewTokenTest::test_non_ascii_token_returns_403_not_500`.

`feeds/views.py:55` calls `hmac.compare_digest(provided_token, store_token.token)` with
the raw query-string value. `hmac.compare_digest` raises
`TypeError: comparing strings with non-ASCII characters is not supported` (reproduced),
so `/feeds/google/us-en.xml?token=café` produces an unhandled 500 instead of a 403.
No data risk; it is error-noise/probing surface. **Fix:** compare bytes —
`hmac.compare_digest(provided_token.encode("utf-8"), store_token.token.encode("utf-8"))`
(the stored token is always ASCII, so a non-ASCII input simply compares unequal).
Add a regression test with a non-ASCII token expecting 403.

### F2 — LOW/MEDIUM — Token rotation is not actually logged (audit-trail claim broken)

**STATUS: APPLIED (2026-07-11).** `StoreFeedToken.rotate()` (`feeds/models.py`)
now emits `logger.info("feeds: token rotated for store %s", self.store_id)`
itself — never the token value. `feeds/admin.py::rotate_token_view` adds a
second log line with the acting admin user's pk (available at the view layer,
not inside the model method). The misleading `_feed_token_rotated` signal
receiver docstring/name in `feeds/signals.py` was corrected and renamed to
`_feed_token_created` (it only ever fires on the initial lazy-creation save,
never on `.rotate()`'s queryset `.update()`) and now logs token *creation*
accurately instead of miscasting it as rotation. Regression test:
`feeds/tests/test_models.py::StoreFeedTokenTest::test_rotate_logs_the_rotation`
(asserts the log line contains the store id and never the raw token value).

ADR-026 D3 states rotation "is logged", and `feeds/signals.py:129-132`
(`_feed_token_rotated`, `post_save` on `StoreFeedToken`) exists for exactly that — but
`StoreFeedToken.rotate()` (`feeds/models.py:159-173`) performs a queryset `.update()`,
which **does not fire `post_save`**. The signal only fires on initial lazy creation.
Net effect: rotations leave no server-side audit record (the admin flash message is the
only trace). **Fix:** emit an explicit `logger.info("feeds: token rotated for store %s", …)`
inside `rotate()` (and/or in `rotate_token_view` with the acting user), and correct or
remove the misleading signal docstring. Never log the token value itself.

### F4 — LOW — `overlay_confirm` does not bind the signup to the requesting store

**STATUS: APPLIED (2026-07-11).** `engagement/views.py::overlay_confirm` now
returns the identical 400 response used for a bad/expired token when
`request.store` is present and differs from `signup.store_id` (matching this
finding's own "Fix" wording verbatim — same status, same message, no oracle).
Regression test:
`engagement/tests/test_views.py::OverlayDeDoubleOptInTest::test_confirm_token_from_another_store_returns_400_and_does_not_confirm`
(asserts 400 + signup not confirmed + marketing not granted).

Note: the task instructions that triggered this fix paraphrased the expected
status as "404 on mismatch", but this finding's own "Fix" text says "return
the same 400 response ... identical to the bad-token message — no oracle". A
distinct 404 would itself be an oracle (revealing "this token is well-formed
but for another store" vs. the 400 a malformed/expired token gets), so the
implementation follows this finding's explicit text: 400, identical message.
Flagging this discrepancy for human confirmation rather than silently picking
one reading.

`engagement/views.py:236` loads the `LeadSignup` via `cross_store_unsafe()` purely by
the signed `lead_signup_id`, with no check that `signup.store == request.store`. A
confirmation token minted for store A's signup is accepted when presented on store B's
host. Exploitability is negligible (the token is `django.core.signing`-signed with a
dedicated salt and 7-day expiry, only the email recipient holds it, and the action is
the one the recipient intended), but it breaks the codebase's tenant-isolation
convention ("every DB lookup is store-scoped"). **Fix:** after loading, return the same
400 response when `request.store` is present and differs from `signup.store_id` (keep
the error message identical to the bad-token message — no oracle).

### F5 — INFO — Honeypot success payload is fingerprintable

**STATUS: APPLIED (2026-07-11).** `engagement/views.py::overlay_signup`'s
honeypot branch now returns `{"ok": true, "action": "display_message",
"message_html": "", "redirect_url": "", "code": null}` — the same key set as
a real success payload, with plausible-but-useless placeholder values (never
a real campaign lookup or discount code). Regression test:
`engagement/tests/test_views.py::OverlaySignupAntispamTest::test_honeypot_payload_has_same_key_set_as_real_success`.

`overlay_signup`'s honeypot branch returns
`{"ok": true, "action": "display_message", "message_html": ""}` while a real success
also carries `redirect_url` and `code` keys and a non-empty message. A bot comparing
payload shapes can detect it was honeypotted, weakening the "never reveal detection"
intent (`pages/antispam.py` contract). **Fix (optional):** return the same key set
(e.g. include `redirect_url: ""`, `code: null` and the campaign's rendered
success message with `{code}` blanked).

### F6 — LOW/MEDIUM (hardening) — `json.dumps` embedded with `|safe` does not escape `<`

**STATUS: APPLIED (2026-07-11).** Added `_json_script_escape()` to
`engagement/slot_providers.py` (same transformation as
`pixels.registry._json_script_escape` / `storefront.views._serialize_jsonld`
— escapes `&`, `<`, `>` to `\uXXXX`), applied to both `config_json`
(`LeadCaptureOverlayProvider.render()`) and `entries_json`
(`SocialProofProvider.render()`) before they reach the `|safe` templates.
Regression tests (hostile slug containing `</script>`, bypassing the
reserved-slug validator the same way a raw `.update()` or a future
signal/import path could):
`engagement/tests/test_slot_providers.py::OverlayHostileSlugEscapeTest::test_hostile_slug_in_excluded_paths_is_escaped_in_config_json`
and
`engagement/tests/test_slot_providers.py::SocialProofHostileSlugEscapeTest::test_hostile_slug_in_product_url_is_escaped_in_entries_json`.

Two inline JSON blocks are emitted with `{{ …|safe }}`:
- `storefront/templates/storefront/partials/overlay_themes/_base.html:101`
  (`config_json` — includes `excluded_paths` derived from `Permalink.slug`), and
- `storefront/templates/storefront/partials/social_proof_toast.html:45`
  (`entries_json` — includes `product_url` derived from `Permalink.slug`).

`Permalink.slug` is a plain `CharField(max_length=500)` (not a `SlugField`; its only
validator is the reserved-slug check), so a hand-entered slug containing `</script>`
would terminate the JSON `<script>` block and inject markup into every storefront page
rendering the widget. The only principal who can author slugs is a store admin — the
same trust level as pixels (arbitrary script by design), so this is not an
unauthenticated XSS. It is still cheap to close: escape `<` as `<` before
embedding (the exact transformation Django's `json_script` filter applies), e.g.
`json.dumps(config).replace("<", "\\u003c")` in
`engagement/slot_providers.py` (both providers). The social-proof `text` values are
already HTML-escaped server-side (autoescaped `blocktrans` render), so `entries_json`
is currently unexploitable via product titles; the slug-derived URL is the only vector.

---

## Area-by-area assessment

### 1. Feeds — token scheme — CLEAR-WITH-NOTES

- **Constant-time compare:** verified — `hmac.compare_digest` at `feeds/views.py:55`,
  never `==`. (But see F1 for the non-ASCII edge.)
- **Entropy/generation:** `secrets.token_urlsafe(32)` (256 bits) on lazy creation
  (`StoreFeedToken.get_or_create_for_store`, race-safe via `select_for_update` inside
  `transaction.atomic`). Global `unique=True` on the column is harmless (collision
  probability negligible).
- **Rotation:** `rotate()` is a single atomic `UPDATE`; the view reads the row per
  request, so the old token dies immediately — pinned by
  `feeds/tests/test_views.py` (rotation → old token 403) and
  `test_admin.py::test_rotate_token_post_replaces_the_token`. Rotation is one click in
  admin with an explicit warning — "easy" confirmed. "Logged" is NOT confirmed → F2.
- **Cross-store reuse:** impossible by construction — host→store resolution
  (`request.locale`) + `.for_store(store)` on both the token and the target lookup;
  covered by the AC-213 cross-tenant tests (both axes) in `feeds/tests/test_views.py`.
- **Leak surface:** token appears in feed URLs configured in Merchant Center/Business
  Manager and in the admin card HTML — acknowledged by ADR-026 Risks and acceptable by
  design *provided rotation is real* — which is exactly what F3 currently undermines.
- **Ordering:** token check runs before any provider/language/target lookup — wrong
  token is always an empty-body 403 regardless of whether the combination exists (no
  existence oracle). Verified in code and tests.

### 2. Feeds — file serving — BLOCKED (F3)

- **Path traversal:** none. The URL regex constrains segments
  (`[a-z]+`, `[a-z]{2}`, `[a-z]{2}(-[a-z]{2})?`), and — more fundamentally — no request
  input ever reaches a filesystem path: the view serves `FeedTarget.file_path` from the
  DB, and that path is composed at generation time (`_feed_file_path`) exclusively from
  the store PK, the provider key (frozen `TextChoices`), the `ShippingCountry`-mirrored
  2-char country code, and the `StoreLanguage.lang_code` — all admin/model-controlled,
  none request-controlled. A crafted FeedTarget cannot be created through any exposed
  surface (rows are reconciled only by `feeds/tasks.py`; no admin add form for targets).
- **Content type:** fixed `application/xml; charset=utf-8`; `FileResponse` only —
  no directory listing surface in the Django path.
- **Atomic serve:** `PENDING`/missing-file → `503 Retry-After: 1800`; `ERROR` keeps
  serving the previous good file (never blanks a live catalog) — matches ADR-026 D3.
- **The blocker:** the *storage location* (inside `MEDIA_ROOT`) makes the entire token
  gate bypassable — see F3 above.

### 3. Feeds — data exposure — CLEAR

- `build_feed_items` (`feeds/items.py`) filters `status=ACTIVE` only; draft/archived
  products are never candidates. Non-default languages require a **published**
  `ProductTranslation` (belt-and-suspenders re-check even though permalink existence
  already implies it). `quotation` variants are excluded entirely; `presale` without a
  date is excluded. No cost/margin fields exist on these models and none are read; no
  customer data is in the pipeline; links go through the permalink resolver
  (canonical primary URLs — no A/B variant slugs).
- **Escaping:** every emitted value passes `xml.sax.saxutils.escape()`
  (`feeds/renderer.py::_write_element`); tags come from code-defined field maps, never
  data. AC-210 dangerous-character coverage exists in tests.
- **exclusions_json:** `{"reason", "product_id", "variant_id"}` only — surfaces solely
  on the store-admin card (gated `apps`/full); nothing public. Internal IDs shown to
  the store's own staff are acceptable.
- `last_error` (admin-only) stores `str(exc)[:2000]` — no credentials exist in this
  pipeline (feeds have no secrets by design, `required_settings=[]`); acceptable.

### 4. Feeds — generation tasks — CLEAR-WITH-NOTES

- **DoS via product churn:** storefront traffic (e.g. inventory decrement on checkout)
  only executes one `UPDATE … SET is_dirty=true` per save via signals — actual
  regeneration happens exclusively in the 30-min beat task and nightly rebuild, each
  dirty target built at most once per tick regardless of how many saves occurred.
  Unbounded regeneration from traffic is not possible. Fan-out from `CurrencyRate`
  saves marks all stores dirty but is bounded (≤ daily) as the ADR argues.
- **Debounce semantics:** `is_dirty` is cleared *before* the build
  (`_process_target`), so a mid-build change re-marks and the next tick regenerates —
  no lost update. Idempotent, re-runnable.
- **Atomic replace:** verified — `tempfile.mkstemp` in the destination directory +
  `os.replace()`, temp file removed on failure; a fetcher can never observe a partial
  feed. Covered by `feeds/tests/test_tasks.py`.
- **Reconciliation:** disabled provider/language or removed country deletes the target
  row and its file (no zombie files). Orphan deletion is best-effort on the file
  (logged), row deletion is authoritative — acceptable.
- Note: signal receivers run on hot model saves; each is a single bounded UPDATE —
  acceptable at current scale (matches ADR risk note).

### 5. Engagement — lead signup POST — CLEAR-WITH-NOTES

- **CSRF:** enforced — `CsrfViewMiddleware` active (`webecom/settings/base.py:73`),
  neither view is `csrf_exempt`, the overlay JS sends `X-CSRFToken` from the cookie,
  and the provider calls `get_token(request)` to guarantee the cookie exists.
- **Antispam:** `honeypot_triggered` (silent success, **no rows written, no customer
  created, no code returned**) + `rate_limit_exceeded(scope="lead_capture")`
  5/IP/store/hour → 429. Production enforces a shared Redis cache
  (`ImproperlyConfigured` guard in production.py), so the limiter is atomic across
  workers. `REMOTE_ADDR` only (X-Forwarded-For never parsed) — the existing
  release-checklist proxy note applies. See F5 for the payload-shape nit.
- **Email validation:** Django `validate_email`; stored lowercased/stripped.
- **255 name truncation:** silent but acceptable — the name is optional, feeds only
  `Customer.first_name` via the merge-never-overwrite service, and truncation is
  strictly bounding, not semantic. Documented here as reviewed-and-accepted.
- **Enumeration:** duplicate signup returns a byte-identical success payload (same
  message, same shared code) without incrementing conversions — the response does not
  reveal prior signup. Residual timing difference (INSERT vs constraint hit) is not
  practically exploitable at 5 req/hour.
- **Confirm token (DE double opt-in):** `django.core.signing` with dedicated salt
  `engagement_lead_capture_confirm`, `max_age` 7 days, payload = signup PK only.
  Tamper/expiry → uniform 400. Replay is idempotent (`confirmed_at is None` guard).
  Cross-store binding gap → F4 (low).
- **Open redirect:** none — no `next`/return-URL parameter exists on any endpoint.
  `redirect_url` is merchant-configured (`URLField` → http/https only, so
  `javascript:` URIs are rejected at validation) and returned only to the merchant's
  own storefront JS.
- Note (accepted): `get_or_create_customer` runs before the duplicate check, so
  attacker-chosen emails create Customer rows — bounded by the rate limit; the rows
  are the store's own CRM data and carry no third-party PII beyond the email entered.

### 6. Engagement — reward issuance — CLEAR

- **Idempotency under concurrency:** `LeadSignup` unique `(store, campaign, email)` +
  `CampaignReward` unique `(store, campaign_id, recipient_email)`; `issue_reward` wraps
  its INSERT in a nested `transaction.atomic()` (savepoint) and treats
  `IntegrityError` as already-issued — safe inside the caller's outer transaction.
  Concurrent-duplicate behaviour is covered by `engagement/tests/test_views.py`.
- **Cross-campaign farming:** one email can obtain each campaign's *shared* code once
  per campaign; possession is harmless because redemption is bounded — verified: the
  redemption path enforces `per_email_limit` (`discounts/service.py:77`) and the model
  default is `1` (`discounts/models.py:109-112`). The ADR's claim holds. Residual
  exposure (merchant links a code with a high limit) is a merchant configuration
  choice, as ADR-027 records.
- **No issuance on honeypot/429:** verified — the honeypot branch returns before any
  DB write; the 429 branch returns before campaign resolution. The code appears in the
  success response by design (shown after signup).
- `campaign_id=f"lead_capture:{campaign.pk}"` — stable opaque ref, rename-proof, and
  namespaced so it can never collide with `campaigns.*` reward refs.

### 7. Engagement — social proof data — CLEAR

- **Anonymous-only enforced at BOTH layers, verified against hostile rows:**
  - Write: `SocialProofSettings.clean()` raises for any non-anonymous mode while
    `NAMED_SOCIAL_PROOF_MODES_ENABLED` is `False`, and the admin form offers only
    `anonymous`.
  - Read: `get_social_proof_entries` overrides `effective_mode` to `ANONYMOUS`
    regardless of the stored value — a hostile/legacy row with
    `display_mode='first_name_city'` still renders anonymously (fail-closed). The
    named-mode branches in `_resolve_display_info` are unreachable until the flag
    flips (which the ADR requires to trigger a fresh Safety review).
- **Exact serialized fields:** each entry is `{"text", "product_url"}` only. `text` is
  composed server-side from: display mode, country (from the shipping-address
  snapshot), localized product title, and a coarsened relative-time bucket — the raw
  `placed_at` never reaches the client. No surname, email, order number, or amount is
  ever read by `_resolve_display_info`/`_compose_sentence`. Anonymized customers
  (`anonymized_at`) are forced anonymous even after the flag flips. Blank first name →
  anonymous. Pinned by `engagement/tests/test_service.py` privacy assertions.
- **Cache keying:** `engagement:social_proof:{store.pk}:{lang}:{effective_mode}` —
  store and language are both in the key, and the key uses the *effective* (gated)
  mode, so a future flag flip cannot serve stale named-mode entries under an anonymous
  key or vice versa. Within (store, lang) the composed URLs are deterministic
  (StoreLanguage↔domain is 1:1), so cross-domain bleed is not possible.
- **No public endpoint** (D7 inline embed) — confirmed: `engagement/urls.py` exposes
  only the three T034 routes. No fabrication path exists (empty window → empty list →
  template renders nothing), pinned by tests.

### 8. Engagement — overlay JS — CLEAR-WITH-NOTES

- **Escaping in all 4 themes:** every theme template
  (`centered_light`/`photo_left_dark`/`photo_right_accent`/`top_banner`) is a thin
  include of `overlay_themes/_base.html`, so there is exactly one markup surface.
  Merchant strings (`headline`, `body`, `cta_label`, `dismiss_label`, the `aria-label`
  attribute) are Django-autoescaped — no `|safe` on any merchant text. The toast
  renders entry text via `textContent` and `href` from a resolver-built path — no
  HTML sink. The two `|safe` JSON blocks are the one hardening gap → F6.
- **`message_html` innerHTML sink:** the success message is merchant-authored
  (`success_message` + the merchant's own discount code string) and is inserted with
  `innerHTML` — deliberate merchant-trust HTML, the same trust level as pixels
  (arbitrary script by design). No shopper-controlled data ever enters this string
  (the shopper's own name/email are never echoed). Accepted; keep it documented in
  the model help text if edited later.
- **Interpolated scalars:** `{{ interval_seconds }}`, `{{ first_delay_seconds }}` in
  the toast script are `PositiveIntegerField`s with min/max validators — safe.
- **Frequency-cap localStorage:** keys hold only path strings (capped at 50) and epoch
  timestamps (pruned to the cap window) — no identifiers, no PII; all reads/writes are
  try/catch-guarded (private-mode safe). Impressions are stamped at `show()`, so a
  dismissed overlay still counts against the cap (matches D4). The visited-path list
  is written on every page view while a campaign is active — client-side only, per the
  accepted D4 exemption reading.
- **Redirect:** `window.location.href = payload.redirect_url` — merchant URLField
  (http/https only), server-supplied; not attacker-influenced.

### 9. Engagement — email confirm flow — CLEAR-WITH-NOTES

- **Header injection via campaign-configured text:** the platform subject template is
  `'Please confirm your subscription to {{ store.name }}'`; `campaign_name` appears
  only in the (autoescaped) body. Even if a merchant-controlled value (store name, or
  a store-override `EmailTemplate` subject) contained a newline, Django's `send_mail`
  raises `BadHeaderError` on any header newline, which `send_transactional_email`
  catches and records as a failed send — fail-closed. Recipient is a validated email
  passed as a list element, not parsed from a header string.
- **Token in URL logged anywhere?** The confirm URL (containing the signed token)
  exists in: the recipient's mailbox (inherent to the pattern), and web-server access
  logs when clicked (standard for email-confirm links; 7-day expiry and
  confirm-only power bound the exposure). It is **not** persisted server-side: the
  `SentEmail` audit row stores template id, recipient, subject and status — not the
  body — and `_send_confirmation_email` logs only PKs on failure. Acceptable.
- The confirm view performs a state change on GET — standard and intentional for
  email links; idempotent; uniform error responses (no signup-existence oracle beyond
  what the signed token already proves).
- `is_de_targeting_store` (active `de` StoreLanguage) over-approximates DE-targeting
  (catches AT/CH German storefronts) — over-inclusion is the safe direction for a
  consent control. The legal sufficiency question remains on the external legal track
  per ADR-027; not an engineering blocker.

---

## Conditions and recommendations recap

**Blocking (release gate):**
1. **F3** — move feed files out of `MEDIA_ROOT` (preferred) or ship + verify an
   explicit web-server deny rule for `/media/feeds/`, with a human curl-verification
   checklist item on the production VPS.

**Recommended before or shortly after release (non-blocking):**
2. **F1** — byte-encode both sides of the feed-token comparison; add a non-ASCII-token
   regression test (expect 403, not 500).
3. **F2** — log token rotation server-side (store id + acting user, never the value);
   fix the misleading signal docstring.
4. **F4** — reject confirm tokens presented on a different store's host (uniform 400).
5. **F6** — escape `<` as `<` in `config_json` / `entries_json` before `|safe`
   embedding (json_script-style), in `engagement/slot_providers.py`.
6. **F5** (optional) — make the honeypot success payload key-identical to a real
   success payload.

**Verdict (original audit, superseded):** APPROVED WITH CONDITIONS — engagement
(T034/T035) releasable as-is with the recommended fixes queued; feeds (T033)
blocked solely on F3. **See "Re-verification and FINAL VERDICT" at the top of
this document (Safety Agent, 2026-07-11): all findings verified fixed, the
feeds BLOCKED verdict is lifted, and the final verdict is APPROVED WITH
CONDITIONS (deploy-checklist items only).**
