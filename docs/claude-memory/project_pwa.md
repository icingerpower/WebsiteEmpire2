---
name: project-pwa
description: Storefront PWA (installable + offline) shipped ADR-067 P1+P2; Web Push deferred to ADR-068 (DPO-gated)
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-05T08:30:53.675Z
---

Storefront PWA for Pradize (WebsiteEcom), shipped 2026-09-04 (ADR-067 P1+P2, commit `1daa999`).

- New thin `pwa/` app (views only, no models): `manifest.webmanifest` (per store+language, `?lang=` validated, name=`store.name`, short_name auto-truncated, icons from store theme media or bundled defaults in `storefront/static/storefront/pwa/`), `sw.js` (hand-rolled, no Workbox/dependency), `/_offline/` branded page. Mounted via `pwa/urls.py` before the storefront catch-all (mirrors sitemaps/robots root-file pattern).
- Service worker is ALLOW-LIST-FIRST: non-GET + cross-origin pass through; `NEVER_CACHE_PREFIXES` (cart/checkout/orders/currency/account/admin/webhooks/chat/api/feeds/_consent/_analytics…) network-only; navigations network-first→`/_offline/` (HTML never cached → no stale prices, ties to [[project_buyer_currency_charging]]); images cache-first capped; `/static/` SWR; skips writing any response with Set-Cookie / Cache-Control private. Version caches via `PRADIZE_ASSET_VERSION`.
- Gated on `settings.PWA_ENABLED` (default True) AND `Store.pwa_enabled` (default True, migration stores/0019). When disabled, `sw.js` serves a self-unregistering worker (purges `pradize-*` caches + `unregister()`) so installed clients roll back — the real kill switch.
- `manifest.webmanifest`/`sw.js` are in `RESERVED_TOP_LEVEL_SLUGS` (no permalink can shadow the worker). The 3 pwa routes are in `SF_LANG_WRITE_EXEMPT_PREFIXES`.
- Safety-critical fixes made pre-release (both revert-proven): offline page must set `request.suppress_third_party_scripts=True` (else it baked one shopper's consent/pixels into the shared cache); kill-switch must serve the unregister worker. Do not regress either.

**Web Push (ADR-068) IS built** (2026-09-05, commits `e948b88` + `7bc3298` + `b4ea262`), enable-ready but `WEB_PUSH_ENABLED` default OFF. New `webpush/` app (`PushSubscription`, subscribe/unsubscribe endpoints rate-limited + flag-gated), platform VAPID keys from env (`pywebpush`; private key never logged), Celery fan-out with 410 cleanup + 90-day purge, additive SW `push`/`notificationclick` handlers (never-cache router untouched), and a campaigns push channel (`CampaignType.PUSH_SEQUENCE`, `PushStep`/`PushSend`, `send_campaign_push`, signed same-origin click-redirect). Consent (`consent_at` + a `push_notifications` purpose, no CONSENT_POLICY_VERSION bump) is the only hard per-recipient gate — NO DPO gate (Cédric is the data authority; enable directly). iOS 16.4+ installed-PWA only. Push-issued coupons (D-6) deferred. Study: `docs/proposals/pwa-and-web-push-feasibility-study.md`. Before flipping the flag: set VAPID env keys (fail-loud if missing when enabled).

To test on :8099 (see [[project_local_8099_scratch_env]]): the scratch DB is migrated; the `--noreload` server must be RESTARTED to load the new PWA code. Follow-up: extract a shared `is_pwa_enabled(store)` helper (logic duplicated in `pwa/views.py` + `storefront/context_processors.py`).
