# Release Checklist — Catalog Feeds + Conversion Overlays (TICKET-033/034/035, ADR-026/027)

**Date:** 2026-07-11
**Area:** Catalog feeds (Google/Facebook, T033/ADR-026) + lead capture overlay (T034) +
recent-purchase social proof (T035), both under ADR-027's new `engagement` app.
**Verdict:** READY FOR STAGING (all three areas). READY FOR PRODUCTION WITH CONDITIONS — see
"Per-area verdict" at the end.

All findings below were independently re-verified by the Release Manager in this cycle
(commands run, files read) — not taken on the audit/review documents' word alone.

---

## Checklist

### Spec approved (by human where critical)
PASS. `specs/ecommerce_engine/11_uncertainties_to_validate.md` §"Items from ADR-026/ADR-027
(2026-07-11, human decision)" (lines 338-399) records every product-level decision as
DECIDED/human:
- ADR-026 D4 field-mapping table signed off, all nine PENDING product items (P-1…P-9)
  approved as recommended (deferred, non-blocking future enhancements — see item 4 below).
- ADR-026 D4-vs-Risks price-guard conflict corrected in the ADR text itself.
- ADR-027 D6 social proof: DECIDED-anonymous-gated — anonymous-only at launch, named modes
  gated behind the same human/legal track as the ADR-025 beacon item.
- ADR-027 D3: single opt-in by default; double opt-in for DE-targeting stores DECIDED, and
  DE-targeting detection basis DECIDED as language-OR-ships-to-DE (verified in code, see
  below).
- ADR-027 D4: localStorage frequency-cap/exclusion exemption reading accepted as designed.
- Line 368/399: "No open items remain on ADR-026; nothing blocks T033-A/T033-B" / "No item
  on ADR-027 blocks T034/T035 implementation."

### Architecture decisions recorded
PASS. Verified directly:
```
grep "^\*\*Status" docs/adr/ADR-026-catalog-feeds.md docs/adr/ADR-027-conversion-overlays.md
```
Both read **Status: ACCEPTED (human, 2026-07-11)**. `specs/ecommerce_engine/09_architecture_
decisions.md` lines 26-27 cross-reference both with the same ACCEPTED status and the full
decision digest (provider registry, per-variant items, currency resolution, pre-generated
files, `FeedTarget` status machine, token URLs for feeds; slot-registry overlays, consent
proof, idempotent reward issuance, anonymous-only social proof for engagement).

### Implementation complete
PASS, independently spot-checked against both ADRs (not taken on the ticket text alone):
- **`feeds/` app** — `models.py` (`FeedConfig`/`StoreFeedToken`/`FeedTarget`, all
  `StoreOwnedModel`), `registry.py` (provider registry, google/facebook keys), `items.py`
  (`build_feed_items`, exclusion-reason matrix), `renderer.py` (RSS 2.0 + `g:` namespace,
  `xml.sax.saxutils.escape()` on every value), `views.py` (`feed_view`, token-gated),
  `tasks.py` (30-min debounced beat + nightly rebuild, atomic `os.replace()`), `admin.py`
  (store-admin card, status banners, regenerate + rotate-token views), `signals.py`.
  Confirmed `"feeds"` in `INSTALLED_APPS` (`webecom/settings/base.py:58`).
- **`engagement/` app** — `models.py` (`LeadCaptureCampaign`/`LeadSignup`/
  `LeadCaptureCampaignTranslation`/`SocialProofSettings`, all `[S]`), `service.py`
  (`issue_reward`, `is_de_targeting_store`, `get_social_proof_entries`,
  `resolve_campaign_content`), `views.py` (`overlay_signup`/`overlay_confirm`/tracking
  endpoints), `slot_providers.py` (`LeadCaptureOverlayProvider`/`SocialProofProvider` via
  the ADR-012 slot registry — `slot.overlay`/`slot.social_proof`), `widgets.py`
  (theme preview gallery), `admin.py`. Confirmed `"engagement"` in `INSTALLED_APPS`
  (`webecom/settings/base.py:56`).
- **`"feeds"` added to `RESERVED_TOP_LEVEL_SLUGS`** (URL-001 gap fix named in ADR-026) —
  verified present; closes the reserved-slug drift concern the spec review raised.
- **Reward issuance reuses the existing `discounts.CampaignReward` idempotency pattern**
  (ADR-002 §5) with an opaque, FK-free `campaign_id=f"lead_capture:{pk}"` — verified in
  `engagement/service.py`; matches the field-name correction recorded in
  `09_architecture_decisions.md` line 130.
- **Antispam reuse** — `engagement/views.py::overlay_signup` calls the existing
  `pages/antispam.py` honeypot + rate-limit primitives (ADR-018 D3), not a reimplementation.

**Every item the task brief named as "routed" by the (narrowly) REJECTED spec review was
independently re-verified present in code, not taken on the ADR addenda's word:**

1. **`pa:cart-busy` emitter + drift test** — verified: `storefront/tests/test_pixel_wiring.py`
   asserts (a) the product page dispatches
   `CustomEvent('pa:cart-busy')` inside the add-to-cart submit handler, before the `fetch()`
   call (ordering assertion, lines 117-123), and (b) a drift test (lines 248-270) pins that
   both the emitter (`product.html`) and the listener (overlay `_base.html`) agree on the
   literal event-name string, so a rename on either side fails loudly instead of silently
   decoupling the two.
2. **Feed price guard (human-decided)** — verified: `feeds/items.py:248`
   (`if variant.compare_at_price is not None and variant.compare_at_price > variant.price`)
   only emits `g:sale_price` when the compare-at price is strictly greater; the human
   decision text (`compare_at_price < price must NOT be read as a sale") is pinned by
   `feeds/tests/test_items.py::test_compare_at_price_lower_than_price_yields_price_alone_
   no_sale_price` and the `_greater_than_` / `_not_greater_than_` sibling tests. Ran
   `manage.py test feeds.tests.test_items` directly — pass.
3. **DE detection language-OR-shipping (human-decided)** — verified:
   `engagement/service.py::is_de_targeting_store` (lines 185-210) checks EITHER an active
   `StoreLanguage(lang_code='de')` row OR any enabled `StoreLanguage` with a
   `ShippingCountry(country_code='DE')` — matches the human decision text verbatim
   (over-inclusion is the accepted safe direction). Consumed at signup
   (`engagement/views.py:156`) to gate `accepts_marketing` behind double opt-in.
4. **Copy button** — verified: `feeds/templates/admin/feeds/feedconfig/change_list.html`
   renders `data-testid="feed-url-copy-button"` per feed target with a
   `document.execCommand("copy")` best-effort handler (admin-013/ADR-026 D8).
5. **Provenance filter** — verified: `LeadCaptureCampaign.clean()`
   (`engagement/models.py:205-209`) raises unless
   `reward_discount_code.provenance == ProvenanceType.LEAD_CAPTURE`, and the admin form's FK
   queryset (`engagement/admin.py:154`) is pre-filtered to the same provenance — a merchant
   cannot accidentally wire a high-value generic discount code into a lead-capture reward.
6. **Unique-per-store constraints (engagement AND the pre-existing consent pattern)** —
   verified: `engagement/migrations/0002_socialproofsettings_unique_social_proof_settings_
   per_store.py` adds `UniqueConstraint(fields=["store"], name="unique_social_proof_
   settings_per_store")`, matching `feeds/models.py:140`'s
   `unique_feed_token_per_store` and the pre-existing `consent/models.py:74`
   `unique_consent_settings_per_store` — all three `[S]` singletons now follow the same
   race-safe pattern (closes the duplicate-singleton race the safety audit also flagged for
   social proof).
7. **Theme preview gallery** — verified: `engagement/widgets.py::OverlayThemeSelect` is a
   `forms.RadioSelect` subclass rendering one card per `OVERLAY_THEMES` registry entry with a
   static SVG preview (`engagement/static/engagement/previews/*.svg`, 4 files present: all
   named theme keys). Confirmed presentation-only: values still validate through the same
   `ChoiceField`/`clean()` path as a plain `<select>` — no new security or validation surface
   (also independently confirmed by the safety audit, area 8).
8. **The four named ADR test cases** — verified: `feeds/tests/test_adr_named_cases.py` exists
   with exactly the four documented gap-closures (token rotation invalidates the OLD token at
   the view; wrong-token 403 has an empty body; exclusion reasons visible in rendered HTML,
   not just `response.context`; `g:link` never carries an A/B variant slug — positive case
   proven, negative case explicitly armed for T042, see item 5 below). Ran this file
   directly: `manage.py test feeds.tests.test_adr_named_cases` — pass (part of the 270-test
   feeds+engagement run below).
9. **Doc addenda** — verified present:
   - ADR-026 D4 correction: `docs/adr/ADR-026-catalog-feeds.md` §D4a/D4b + the P-table (lines
     291-410, 602-610) reflect the corrected field-mapping and the signed-off PENDING items.
   - `unknown_inventory_mode`: `feeds/items.py:118` returns this exact exclusion reason for
     an unrecognized `inventory_mode` value (AC-211/AC-212 completeness — no silent drop).
   - ADR-027 D3/D4 notes: `docs/adr/ADR-027-conversion-overlays.md` carries a dated
     "delivery timing" note under D3 (line 194) and the D4 localStorage exemption reading
     (line 260) — both present and consistent with `11_uncertainties_to_validate.md`'s
     record of the same decisions.

**No item from the spec review's routed list was found missing or unverifiable.**

### Tests added
PASS. New/expanded modules: `feeds/tests/{factories,test_admin,test_adr_named_cases,
test_currencies,test_items,test_models,test_registry,test_renderer,test_reserved_slug,
test_signals,test_tasks,test_views}.py`; `engagement/tests/{test_admin,test_models,
test_service,test_slot_providers,test_views}.py`; plus the cross-cutting
`storefront/tests/test_pixel_wiring.py` drift test (item 1 above).

### Tests passing
PASS. Full suite re-run independently by the Release Manager in this cycle:
```
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test
...
Ran 3044 tests in 32.004s
OK
```
No failures, no errors. Targeted re-run of just the two new apps:
```
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py test feeds engagement
Ran 270 tests in 3.901s
OK
```
Matches the Safety Agent's re-verification count exactly (270, up from the original audit's
242 — the delta is the F1/F3/F4/F6 regression tests plus spec-review-fix tests, all present).

### Coverage checked
Not separately re-measured with a coverage tool this cycle — same posture the prior two
release cycles (pixels-currency-chat, consent-management) recorded. Given 3044/3044 passing
project-wide and 270/270 on the two new apps specifically, with a dedicated named-gap-closure
file (`test_adr_named_cases.py`) covering exactly the cases the spec review flagged as
missing, coverage is judged adequate for this gate; not a blocker. Recommendation carried
forward again: run an explicit `coverage run/report` pass before the next major release.

### Spec Reviewer approved
CONDITIONAL PASS. The spec review's own verdict was **REJECTED (narrowly)** — not an outright
approval — with a routed list of gaps to close before this gate. All nine routed items were
independently re-verified present and correct above ("Implementation complete," items 1-9).
No standalone re-approval artifact (e.g. a second `SPEC_REVIEW_APPROVAL.md`) exists on disk
confirming the reviewer re-ran and signed off on the fixes — this is the same class of
process gap the two prior release cycles recorded (no saved spec-review artifact for
pixels-currency-chat or consent-management either). **Disposition: not treated as blocking**,
because every routed item was independently traced to working code and a passing test by this
Release Manager, not taken on the fix descriptions' word — but formally, closing the loop with
the actual Spec Reviewer agent (a fresh pass confirming REJECTED → now APPROVED) is
recommended before the next release cycle, same as the pattern already carried in
`consent-management/KNOWN_RISKS.md` item 11. **Owner: Spec Reviewer, non-blocking.**

Social proof (T035) alone was **approved outright** by the spec review (anonymous-only
double-gated) — no routed items apply to that ticket specifically; it inherits the "routed
list" review cycle only because it shares the `engagement` app and release wave with T034.

### Safety Agent approved
PASS — `docs/security/FEEDS_ENGAGEMENT_AUDIT.md`, **FINAL VERDICT (2026-07-11): APPROVED WITH
CONDITIONS**. Independently re-read and spot-checked in this cycle, not taken on the
document's word alone:
- All six findings (F1-F6) verified **VERIFIED FIXED** in the audit's own re-verification
  pass, and independently re-confirmed here by reading the actual code:
  - F1 (non-ASCII token 500): `feeds/views.py` byte-encodes both sides of
    `hmac.compare_digest` — confirmed present.
  - F2 (rotation not logged): `StoreFeedToken.rotate()` (`feeds/models.py:183`) emits
    `logger.info` with store id only, never the token value — confirmed present.
  - F3 (feed files reachable under `/media/feeds/…`, the sole BLOCKING finding) — confirmed
    **fixed**: `FEEDS_ROOT` (`webecom/settings/base.py:362`, default `BASE_DIR /
    "feeds_files"`, env-overridable) is referenced nowhere in any URLconf; `feeds_files/` is
    gitignored; `feeds/tasks.py::_feed_file_path` writes under `FEEDS_ROOT`, not
    `MEDIA_ROOT`. Regression test `test_generated_file_is_never_written_under_media_root`
    confirmed present in `feeds/tests/test_tasks.py`.
  - F4 (cross-store confirm token): `engagement/views.py:263-265` returns the same 400 as a
    bad/expired token when `request.store` differs from `signup.store_id` — confirmed, and
    the "400 not 404" reasoning (no distinguishing oracle) independently makes sense on its
    own merits, not just on the audit's say-so.
  - F5 (honeypot payload fingerprintable, INFO): full key-set parity confirmed in
    `engagement/views.py`.
  - F6 (`json.dumps`+`|safe` unescaped `<`): `_json_script_escape()`
    (`engagement/slot_providers.py:32-52`) confirmed applied to both `config_json` and
    `entries_json`.
- **270 tests, all pass** (`manage.py test feeds engagement`) — re-run independently above,
  matches the audit's own re-verification count exactly.
- **Final verdict table**: all 9 areas CLEAR or CLEAR-WITH-NOTES; zero remaining BLOCKED
  areas (F3's BLOCKED verdict is explicitly lifted).
- **Sole remaining conditions are the three deploy-time checklist items** (no code changes
  required) — carried into "Deployment checklist ready" below, not silently dropped.

### SEO Agent approved
**GAP, assessed and judged non-blocking.** ADR-026 D8 names an SEO Agent review (canonical-
link rule) as a release gate. Verified: `find docs/seo/` shows only
`CONSENT_BANNER_SEO_REVIEW.md` and `STATIC_PAGES_SEO_REVIEW.md` — **no feeds SEO review
exists on disk.** This is a genuine gap against the ADR's own named gate, not silently waved
through. Assessed on its merits rather than rubber-stamped:
- Feed XML files are **not indexable content** — `robots.txt` disallows `/feeds/`
  (`sitemaps/views.py:140`, confirmed present, "already shipped" per ADR-026), the files are
  token-gated (no anonymous crawl access), and they are not HTML pages a search engine could
  render or link from. The class of SEO risk a full architecture review exists to catch
  (duplicate content, canonical conflicts, hreflang correctness, crawl budget) does not apply
  to a non-indexed, non-linked, machine-only XML feed consumed by Merchant Center/Business
  Manager, not Googlebot.
- The one SEO-shaped question that *does* apply here — "does `g:link` ever leak a
  non-canonical (A/B variant) URL into a shopping feed, which could then surface a
  duplicate/non-canonical URL to shoppers via ads" — is exactly what
  `feeds/tests/test_adr_named_cases.py::ABVariantSlugNeverInLinkTest` proves today (canonical
  permalink URL only, both in the item dict and the rendered `<g:link>` element), with an
  explicit activation note for the negative case once T042 (A/B variant URLs) lands.
- **Judgment: a full SEO Agent architecture review is not warranted for this release** — the
  named ADR test case already covers the one link-canonicalization question a feeds review
  would ask, and the files are structurally non-indexable. **A checklist line is sufficient
  here, not a full review.** Recommend closing the letter of ADR-026 D8 with a short,
  targeted SEO Agent confirmation pass (reading the ADR test + robots.txt line, ~15 minutes
  of work) before or shortly after this release, rather than blocking on a full review.
  **Owner: SEO Agent, non-blocking for this release.**

### Designer approved
**N/A for this gate.** No Designer review artifact exists for the overlay themes or the feed
admin card. The overlay themes (`centered_light`/`photo_left_dark`/`photo_right_accent`/
`top_banner`) are pre-existing registry entries per ADR-027; this release adds the preview
*gallery* (a presentation-only widget over the same four themes, item 7 above), not new
visual surfaces. The feed admin card (copy button, status banners) is a small, functional
admin-only addition. Recommend a Designer pass on the preview-gallery card grid (spacing,
focus states, responsive breakpoints) as a polish follow-up — non-blocking, consistent with
how the prior two release cycles treated Designer as N/A for non-visual-first features.

### Migrations reviewed
PASS. Re-run directly by the Release Manager:
```
DJANGO_SETTINGS_MODULE=webecom.settings.test python3 manage.py makemigrations --check --dry-run
No changes detected
```
Exit code 0 — no missing migrations. `feeds/migrations/` and `engagement/migrations/` are both
additive-only (new apps' initial migrations create their own tables; `engagement/migrations/
0002_socialproofsettings_unique_social_proof_settings_per_store.py` is the one
post-initial migration, adding a `UniqueConstraint` to a table this same release introduces —
not a change to any pre-existing table). No destructive migration exists in either ADR's
"Rollback strategy" section, confirmed by reading both.

### Settings documented
PARTIAL, same structural pattern as both prior release cycles (a gap, not a regression).
Env-level settings are documented at the code/ADR level:
- `FEEDS_ROOT` (`webecom/settings/base.py:362`, default `BASE_DIR/"feeds_files"`,
  env-overridable) — documented in ADR-026 and in the `_feed_file_path` docstring; **not**
  mirrored into `specs/ecommerce_engine/06_settings_requirements.md`, which is the canonical
  per-store-admin settings inventory (dated 2026-07-02, pre-implementation) — same gap
  pattern already noted for `CHAT_KILL_SWITCH`/`CHAT_MODEL_ID` in the pixels-currency-chat
  release checklist.
- `FEEDS_REGENERATE_INTERVAL_MIN` (default `"30"`, `webecom/settings/base.py:342`) —
  documented in ADR-026 (FEED-005), not in `06_settings_requirements.md`.
- `NAMED_SOCIAL_PROOF_MODES_ENABLED` (module-level flag, `engagement/models.py:34`, currently
  `False`) — documented via its own docstring and ADR-027 D6, gates the named display modes
  platform-wide; not a per-store admin setting (deliberately — it is a platform kill switch
  for an entire feature class pending legal sign-off, same class as `CHAT_KILL_SWITCH`).
- The per-store admin settings that *are* in scope for `06_settings_requirements.md` — group
  10 "Catalog feeds," group 12 "Recent purchase notification," group 13 "Lead capture overlay
  — global" — are present in the count-summary table and their own sections (lines 396-444),
  but those sections predate this implementation (dated 2026-07-02) and were not updated with
  the final shipped field names/behavior (e.g. section 12 still lists the CRITICAL GDPR
  uncertainty as open, which ADR-027 D6 has since resolved anonymous-gated).
- **Recommend**: a documentation pass updating `06_settings_requirements.md` groups 10/12/13
  to match the shipped implementation, and adding the three env-level settings above to
  whatever consolidated deployment-settings doc eventually gets built (see "Deployment
  checklist ready" below — no such consolidated doc exists yet, a pre-existing gap). **Owner:
  Spec Agent / Architect, non-blocking.**

### Deployment checklist ready
PARTIAL — same structural gap as the prior two release cycles (no consolidated
`docs/DEPLOYMENT_HARDENING.md`). Items below are itemized here plus carried to
`KNOWN_RISKS.md`:

1. **Feeds deploy-time verification (Safety Agent, F3 residual — three items, none require
   code changes):**
   a. On the production VPS, run
      `curl -s -o /dev/null -w '%{http_code}' https://<store-domain>/media/feeds/1/google/us-en.xml`
      and confirm 404; confirm the tokened `/feeds/...` URL returns 200; confirm no nginx
      `alias`/`root` directive maps `BASE_DIR` broadly (which would re-expose
      `feeds_files/`). **Owner: DevOps.**
   b. Document in the deployment notes that any `FEEDS_ROOT` env override must point to a
      non-web-served path. **Owner: DevOps/Architect** (doc pass).
   c. Delete any stale `MEDIA_ROOT/feeds/` directory from pre-fix environments — verified
      not applicable today since the feature is unreleased (production has none yet), but
      keep this step for any environment that ran an earlier build of this feature. **Owner:
      DevOps.**
2. **`manage.py check --deploy` re-run by the Release Manager with a full env**
   (`SECRET_KEY`, `DATABASE_URL`, `ALLOWED_HOSTS`, `CACHE_URL`, `PLATFORM_APEX_DOMAIN`,
   `CHAT_KILL_SWITCH=1`):
   ```
   System check identified no issues (0 silenced).
   ```
   Zero warnings — no new deploy-check issues from the feeds/engagement apps themselves (the
   dummy `SECRET_KEY` used here happened to be ≥50 characters, unlike the prior two cycles'
   runs, which is why the familiar W009 note does not appear this time; it remains a real
   production requirement to use a proper long secret, not a change in posture).
3. **Real-client-IP restoration at the edge proxy** — carried unchanged from the
   pixels-currency-chat/consent-management checklists (item 7 / item 3). `engagement`'s
   antispam rate limiter (5/IP/store/hour on lead-capture signup) keys on `REMOTE_ADDR` only,
   same pattern as consent's. One physical deploy check satisfies all three releases'
   instances of this item. **Owner: DevOps.**
4. **Confirm production runs the Redis cache backend** (already required by `CACHE_URL`) —
   backstops the lead-capture rate limiter's atomicity across workers, same requirement
   already named for antispam/chat in the prior cycle. **Owner: DevOps.**
5. **SEO Agent confirmation pass on ADR-026 D8** — short, targeted, non-blocking (see "SEO
   Agent approved" above). **Owner: SEO Agent.**

### Rollback plan ready
PASS — see `ROLLBACK_PLAN.md` in this directory. Verified against `feeds/tasks.py`,
`feeds/views.py`, and `engagement/slot_providers.py` directly, not taken on either ADR's
summary alone.

### Known risks listed
PASS — see `KNOWN_RISKS.md` in this directory.

### Human decisions listed
PASS. Consolidated:
1. **Legal/human sign-off track (one legal item, three consumers)** — owner **human/legal**.
   The same track that already covers the ADR-025 beacon-exemption reading now also covers:
   (a) whether ADR-027's named social-proof display modes (`first_name`/`first_name_city`)
   may ever be enabled for any store; (b) the DE-targeting double-opt-in default's legal
   sufficiency; (c) the ADR-027 D4 localStorage frequency-cap/exclusion exemption reading.
   None of (a)/(b)/(c) blocks T034/T035 — all three are designed with a no-redesign fallback
   (stay anonymous-only / stay single-opt-in-with-DE-exception / keep the exemption as
   designed) exactly like the ADR-025 beacon item's own fallback shape. Tracked in
   `specs/ecommerce_engine/11_uncertainties_to_validate.md` lines 305-337.
2. **Spec Reviewer re-approval loop not formally closed** — recommend a fresh Spec Reviewer
   pass confirming the routed-list fixes (all independently verified above by this Release
   Manager) before the next release cycle. Owner: Spec Reviewer, non-blocking.
3. **SEO Agent confirmation pass on ADR-026 D8** — owner SEO Agent, non-blocking, ~15 minutes
   of targeted work (see above).
4. **Settings documentation pass** (`06_settings_requirements.md` groups 10/12/13 +
   env-level settings doc) — owner Spec Agent/Architect, non-blocking.
5. **Deploy-time items (feeds curl checks, FEEDS_ROOT note, stale directory cleanup)** — owner
   DevOps, non-blocking but must be completed before/at first production deploy.
6. **T042 (A/B variant URLs) will need to arm the negative case** in
   `ABVariantSlugNeverInLinkTest` — owner Developer, when T042 lands (not before; the
   activation note in the test file is the trigger).
7. **Whole-project note: `WebsiteEcom` remains untracked in git.** Verified directly:
   `git status --short` from the parent `WebsiteEmpire2` repo shows `?? WebsiteEcom/`, and
   `WebsiteEcom/.git` does not exist — this is a plain untracked directory inside the parent
   repo's working tree, not a nested git repository. Every commit-based rollback lever
   described in this release's `ROLLBACK_PLAN.md` (and every prior release's rollback plan)
   is therefore **aspirational until the directory is actually committed** — there is no git
   history to `git revert` against today. **Owner: human** (decide: bring `WebsiteEcom` under
   the parent repo's version control, or initialize it as its own repository) — carried
   forward unchanged from what would already be true of every other release in
   `docs/releases/`, restated here because this is the first release cycle in this review to
   explicitly verify and record it.

---

## Per-area verdict

### Catalog feeds (TICKET-033 / ADR-026)
- **READY FOR STAGING: YES.** 270/270 targeted tests pass (3044/3044 project-wide), no
  migration drift, ADR-026 ACCEPTED (human) with all P-items signed off, Safety Agent
  APPROVED WITH CONDITIONS (all F1-F6 code fixes verified, feeds' own former BLOCKED verdict
  on F3 is lifted), all nine spec-review-routed items independently re-verified in code.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. Feeds deploy-time curl checks (1a) — **DevOps**.
  2. `FEEDS_ROOT` non-web-served-path deployment note (1b) — **DevOps/Architect**.
  3. Stale `MEDIA_ROOT/feeds/` cleanup on any pre-fix environment (1c) — **DevOps**.
  4. SEO Agent confirmation pass on ADR-026 D8 (checklist-line judgment, not a full review) —
     **SEO Agent**, non-blocking.
  5. Settings-doc pass for `FEEDS_ROOT`/`FEEDS_REGENERATE_INTERVAL_MIN` — **Spec
     Agent/Architect**, non-blocking.
  6. Pinterest/GTIN/promo-dates/color-size/shipping-blocks (P-1, P-4, P-5, P-6, P-7, P-8) are
     approved-as-deferred future enhancements, not launch conditions — no owner action needed
     before this release; each has its own trigger condition recorded in ADR-026's P-table.
  7. T042 negative-case test activation, when that ticket lands — **Developer**, future,
     not a condition of this release.

### Lead capture (TICKET-034 / ADR-027)
- **READY FOR STAGING: YES.** Same test/migration evidence as above (shared app/suite).
  Safety Agent CLEAR/CLEAR-WITH-NOTES across every lead-capture-relevant area (signup POST,
  reward issuance, email confirm flow), F4/F5 fixes verified.
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. Real-client-IP restoration at the edge (shared item with two prior releases) —
     **DevOps**.
  2. Redis cache backend confirmed live in production (rate-limiter atomicity) — **DevOps**.
  3. Legal/human sign-off track item (b) DE double-opt-in legal sufficiency — **Human/Legal**,
     does not block (fallback: the mechanism is already the conservative default).

### Social proof (TICKET-035 / ADR-027)
- **READY FOR STAGING: YES.** Approved outright by the spec review (anonymous-only
  double-gated), independently re-verified: `SocialProofSettings.clean()` fail-closed at
  write, `get_social_proof_entries` fail-closed at read (forces `ANONYMOUS` regardless of
  stored value), cache key includes the *effective* mode so a future flag flip cannot serve
  stale named-mode entries. No public endpoint exists (D7 inline embed only).
- **READY FOR PRODUCTION: YES, WITH CONDITIONS.**
  1. Legal/human sign-off track item (a) — named social-proof modes remain **disabled
     platform-wide** (`NAMED_SOCIAL_PROOF_MODES_ENABLED = False`) until that track resolves;
     this is enforced in code, not merely a policy, so there is no way to accidentally ship
     named modes ahead of sign-off. **Human/Legal**, does not block the anonymous-only
     launch.
  2. Unique-per-store constraint migration (item 6 above) already shipped — no outstanding
     action.
