# Rollback Plan — Catalog Feeds + Conversion Overlays (TICKET-033/034/035, ADR-026/027)

**Date:** 2026-07-11

Both new apps (`feeds`, `engagement`) are additive: no existing table's columns were changed,
no existing app's models were touched beyond the small, well-scoped touch points listed below.
Rollback for each app is independent — you can revert `feeds` without touching `engagement`
and vice versa, since neither imports from the other.

---

## What was added / changed

**New app `feeds/`:** `models.py` (`FeedConfig`, `StoreFeedToken`, `FeedTarget`, all
`StoreOwnedModel`), `registry.py`, `items.py`, `renderer.py`, `views.py` (`feed_view`),
`tasks.py` (`regenerate_dirty_feeds` beat task, `rebuild_all_feeds` nightly task,
`_process_target`, `_reconcile_matrix`), `admin.py`, `signals.py`.

**New app `engagement/`:** `models.py` (`LeadCaptureCampaign`, `LeadSignup`,
`LeadCaptureCampaignTranslation`, `SocialProofSettings`, all `[S]`), `service.py`
(`issue_reward`, `is_de_targeting_store`, `get_social_proof_entries`,
`resolve_campaign_content`), `views.py` (`overlay_signup`, `overlay_confirm`, tracking
endpoints), `slot_providers.py` (`LeadCaptureOverlayProvider`, `SocialProofProvider`),
`widgets.py`, `admin.py`.

**Touched existing surfaces (small, well-scoped):**
- `webecom/settings/base.py` — `INSTALLED_APPS` gains `"feeds"` and `"engagement"`;
  `FEEDS_ROOT` and `FEEDS_REGENERATE_INTERVAL_MIN` settings added.
- `.gitignore` — `feeds_files/` (and `media_files/`, a pre-existing gap in the same pattern)
  added.
- `permalinks`/URL reservation — `"feeds"` added to `RESERVED_TOP_LEVEL_SLUGS`.
- `sitemaps/views.py` — `robots.txt` already disallowed `/feeds/` (no change needed this
  cycle; confirmed present).
- `storefront/templates/storefront/product.html` — the `pa:cart-busy` event emitter (small,
  named fix, item 1 of the spec-review-routed list).
- `discounts.CampaignReward` — consumed (not modified) via its existing opaque, FK-free
  `campaign_id` string convention; no schema change to that model from this release.

**Database schema (migrations):** `feeds/migrations/0001_initial` (and any that follow) create
`FeedConfig`/`StoreFeedToken`/`FeedTarget`; `engagement/migrations/0001_initial` creates
`LeadCaptureCampaign`/`LeadSignup`/`LeadCaptureCampaignTranslation`/`SocialProofSettings`;
`engagement/migrations/0002_socialproofsettings_unique_social_proof_settings_per_store`
adds a `UniqueConstraint` to a table this same release introduces. All additive — no
pre-existing table's columns were altered.

**Filesystem:** generated feed XML files live under `FEEDS_ROOT` (default
`BASE_DIR/feeds_files`, gitignored, never web-served) — a new location on disk, not inside
`MEDIA_ROOT`.

---

## Rollback levers, in order of safety/speed

### Lever 1 (fastest, narrowest, no deploy): per-store / per-provider disable

- **Feeds:** set `FeedConfig.is_enabled=False` for the affected provider(s) via `/admin/`.
  **Verified effect:** `feeds/tasks.py::_reconcile_matrix` deletes the now-out-of-scope
  `FeedTarget` rows (and their files, best-effort) on the next beat tick; the feed URL then
  404s (no `FeedTarget` row for the store/provider/country/lang combination). This is
  reversible in the other direction just as fast (re-enable → next beat tick regenerates).
- **Lead capture:** set the campaign's own `is_active`/status flag off, or (per-store) disable
  every campaign. **Verified effect:** `LeadCaptureOverlayProvider.is_enabled()` returns
  `False` for that store/campaign combination — the overlay stops rendering; no code path
  writes `LeadSignup` rows or issues rewards for a disabled campaign (the antispam/rate-limit
  and reward-issuance code paths are only reachable from a rendered, active overlay's POST
  target).
- **Social proof:** set `SocialProofSettings.is_enabled=False` (already the default) via
  `/admin/`. **Verified effect:** `SocialProofProvider` (per D7) renders nothing; no public
  endpoint exists for this feature at all, so there is no other surface to disable.

**Use this lever when:** the problem is isolated to one provider/campaign/store and does not
require a code revert.

### Lever 2 (full rollback): remove the app(s) / git-revert

**Important caveat, verified against code in this cycle:** unlike the consent-management
release's rollback plan (where a `try/except ImportError` fail-closed shim exists at every
cross-app import site), **`feeds` and `engagement` are not imported from any other app** —
`storefront`'s slot-registry mechanism discovers `engagement`'s providers only if the app is
installed and registered; there is no shim to fall back to, because there is nothing else in
the codebase that depends on either app existing. This makes a full removal simpler and lower
blast-radius than the consent rollback, not more complex:

- **Feeds full removal:**
  1. Set every `FeedConfig.is_enabled=False` first (Lever 1) if you want a graceful drain
     rather than an abrupt one — optional, since removing the app stops serving `/feeds/*` at
     the URLconf level regardless.
  2. `python3 manage.py migrate feeds zero` — drops `FeedConfig`/`StoreFeedToken`/
     `FeedTarget` tables. Confirm current migration state with `showmigrations feeds` on the
     actual deployed environment first.
  3. Remove `"feeds"` from `INSTALLED_APPS`, revert the `feeds/` directory and its two small
     touch points (`"feeds"` reserved-slug entry, `pa:cart-busy` emitter in `product.html` —
     the emitter is harmless to leave in place even without the app, since the listener side
     lives in `engagement`'s overlay template, not `feeds`) via git.
  4. Delete the `FEEDS_ROOT` directory on disk (`feeds_files/` by default) — it holds no data
     that isn't trivially regenerable from the catalog, so deleting it is always safe.
  5. Any external Merchant Center / Business Manager feed-URL configuration referencing the
     now-dead `/feeds/...` URLs will start failing (expected — that is the point of a full
     removal); this is an operational notice to Product/Support, not a code concern.

- **Engagement full removal:**
  1. Set every campaign/`SocialProofSettings.is_enabled=False` first (Lever 1) for a graceful
     drain — optional.
  2. `python3 manage.py migrate engagement zero` — drops all four tables, including the
     `LeadSignup` consent-proof rows. **If there is any open legal/audit need to retain
     lead-capture consent proof, export `LeadSignup` via the admin changelist before running
     this** (same caution as the consent-management release's `ConsentRecord` export note).
  3. Remove `"engagement"` from `INSTALLED_APPS`, revert the `engagement/` directory and the
     `pa:cart-busy` listener in the overlay base template via git.
  4. Discount codes already issued via `issue_reward` (`discounts.CampaignReward` rows with
     `campaign_id` prefixed `lead_capture:`) are **not** cleaned up by this rollback — they
     are ordinary, already-redeemable discount codes in the pre-existing `discounts` app and
     are unaffected by removing `engagement`. Decide separately (Product decision, not a code
     concern) whether to deactivate them.

### Rollback procedure (either app)

1. Prefer Lever 1 if the problem is isolated to a provider/campaign/store.
2. Prefer Lever 2 if the problem is systemic (a bug affects every store, or the issue is in
   shared code like `feeds/renderer.py` or `engagement/service.py::issue_reward`).
3. **Migrations:** additive only, so `migrate <app> zero` is a clean, reversible drop with no
   destructive interaction with any pre-existing table.
4. **Post-rollback smoke check:**
   ```bash
   python3 manage.py test storefront.tests.test_theme_conformance
   ```
   (confirms `slot.overlay`/`slot.social_proof` render their empty wrappers cleanly with the
   provider unregistered, same pattern the consent-management release used for its own slot.)

---

## Data

- No financial or order state is touched by any rollback lever in either app — feeds are a
  read-only projection of the catalog; lead capture creates `Customer`/`LeadSignup`/
  `CampaignReward` rows but never modifies `Order`/`Payment` state; social proof only reads
  PAID orders, never writes to them.
- `LeadSignup` rows are this release's consent-proof trail (marketing opt-in evidence, no
  IP address stored) — export before `migrate engagement zero` if retention is needed (see
  above).
- Feed XML files on disk under `FEEDS_ROOT` carry no data that cannot be regenerated from the
  catalog on the next beat tick or nightly rebuild — safe to delete at any time.

## What is NOT rolled back by this plan

- Discount codes already issued through `issue_reward` (`discounts.CampaignReward` rows) —
  these live in the pre-existing `discounts` app and outlive an `engagement` rollback by
  design (a shopper who already received a code keeps it; only future issuance stops).
- Any `Customer` rows created via `get_or_create_customer` during a lead-capture signup —
  these are ordinary CRM data in the pre-existing `customers` app, unaffected by rolling back
  `engagement`.
- External feed-consumer configuration (Google Merchant Center / Meta Business Manager feed
  URLs) — these are configured outside this codebase and must be updated/disabled separately
  if `feeds` is fully removed.
