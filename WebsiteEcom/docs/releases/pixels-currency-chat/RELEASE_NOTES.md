# Release Notes — Pixel Integrations + Currency Display + AI Sales Chat

**Date:** 2026-07-11

## Summary

This release adds three independent storefront/admin capabilities: third-party pixel
tracking (Facebook/Meta, Google GA4, TikTok, Snapchat, Pinterest), a storefront currency
display converter with a matching admin/super-admin currency screen, and an AI-powered
sales-assistant chat widget backed directly by the Claude API.

## Pixel integrations (TICKET-030 / ADR-022)

- New `pixels` admin screen (store admin only): a card grid per provider showing
  Installed/Not Installed status, with install/edit/uninstall actions — matches the
  screenshot spec (admin-018).
- Five providers, one `Pixel` row each per store: Facebook/Meta, Google Analytics 4 (GA4
  only — no legacy Universal Analytics), TikTok, Snapchat, Pinterest.
- Five canonical events mapped per provider: page_view, view_content, add_to_cart,
  initiate_checkout, purchase. Purchase fires exactly once per (order, provider) via an
  insert-before-fire `FiredPixel` claim, gated on `payment_status == PAID`, race-safe under
  concurrent requests.
- Two independent injection defenses on every pixel ID: a strict per-provider regex at
  write time and `|escapejs` at render time.
- The AJAX add-to-cart bridge now correctly distinguishes a successful add from a server
  error: on any non-2xx response it falls back to a normal form submit instead of firing a
  false conversion pixel and navigating to a dead-end URL.
- The thank-you page's automatically-collected page URL is stripped of its (permanent,
  never-expiring) order-access token before being sent to Google Analytics 4 — closing a
  confidentiality exposure for that provider (human decision PIX:P1). This fix is currently
  GA4-only; see Known Risks.
- A data migration retires the legacy `__purchase_guard__` sentinel mechanism, converting it
  into per-provider `FiredPixel` rows so already-PAID orders do not double-fire on upgrade.

## Currency display (TICKET-031 + TICKET-037 / ADR-023)

- New platform-global currency master data: `CurrencyDefinition` (code, name, symbol,
  decimal places, code visibility), `CurrencyRate` (anchored to EUR), seeded with the ~30 ECB
  reference currencies at launch.
- Storefront currency picker and price display: shoppers can browse in a display currency
  different from the store's own transactional currency. Conversion is **display-only** —
  the shopper is always charged, and the order is always recorded, in the store's own
  currency. A mandatory "approximate, converted for display" disclaimer accompanies every
  converted price.
- `.99` psychological rounding (`ceil(raw) − smallest_unit`), applied consistently for both
  2-decimal and 0-decimal (JPY/KRW-style) currencies.
- Optional ECB daily-feed auto-refresh for exchange rates — **off by default**; manual rate
  entry is always authoritative. A super-admin explicitly opts in per the platform-wide
  `CurrencyConverterSettings` singleton.
- Admin/super-admin currency screen (TICKET-037): rate source and refresh cadence
  configuration, per-store currency enablement checklist, and the restored "Code visibility"
  control (`show_code`/`code_placement` — e.g. `"45.99 $ CAD"`), matching the original
  screenshot spec.
- Checkout, the thank-you page, the order record, and transactional emails are guarded —
  tested — to never apply display-currency conversion; they always show the store's own
  transactional currency, exactly as before this release (this was already shipped behavior
  from ADR-015; ADR-023 only adds the *pre-checkout* display conversion and formalizes the
  boundary with regression tests).

## AI sales-assistant chat (TICKET-038 / ADR-024)

- New storefront chat widget: shoppers can ask questions about products, collections, and
  store policies (shipping/refund/terms/contact), answered by Claude with read-only tool
  access to the store's live catalog — no write access, no invented discounts or coupons.
- Per-store opt-in, **off by default**: a store owner must explicitly enable chat and accept
  the AI sub-processor terms (GDPR) before the widget appears or the endpoints accept
  requests — enforced at the endpoint level, not just hidden in the UI.
- Short-lived server-sent-events (SSE) streaming per reply on the existing plain-WSGI stack
  (no new infrastructure/Channels dependency); a `?stream=0` fallback returns the identical
  final text as plain JSON.
- Spend controls: per-session message (30) and token (150K) caps, per-(store, IP) hourly rate
  limits, a store-wide daily message budget enforced independent of source IP, a pre-call
  quota refusal (zero Anthropic calls once a store's monthly budget is exhausted), and a
  `CHAT_KILL_SWITCH` operational override.
- `claude-haiku-4-5` by default with prompt caching (system prompt + tool definitions kept
  above the 4096-token cacheable minimum), per-store model override available.
- 30-day retention: chat sessions and messages are purged automatically by a daily background
  task.
- The chat widget is suppressed on the distraction-free checkout page (matching the same
  rule already applied to the lead-capture overlay and social-proof toast), and is suppressed
  in theme preview mode (matching pixels and the consent slot).

## Notable fixes shipped in this release (security audit follow-through)

- **Pixels:** the AJAX add-to-cart bridge no longer fires a false conversion pixel or
  navigates to a dead-end URL on a failed add-to-cart request. `content_ids_json` is now
  escaped against `</script>` breakout. `Pixel.save()` re-validates the pixel ID format on
  every write path, not just the admin form. `cart`'s internal redirect-safety helper now
  uses Django's own host/scheme validator, closing a backslash-URL bypass.
- **Chat:** the Anthropic API call now has an explicit request timeout, preventing a single
  slow/hung upstream connection from pinning a web worker indefinitely. The chat-settings
  admin now correctly scopes its four policy-page dropdowns to the current store (previously
  the admin form could not render at all for this feature); the tool layer independently
  re-verifies store ownership of any resolved policy page as defense-in-depth. Error messages
  sent to the shopper are now generic — the real exception detail stays in server logs only.

## What is explicitly NOT in this release

- **GDPR/ePrivacy consent gating for pixels.** Pixels fire with no consent-banner check.
  This is a deliberate, named-out-of-scope cross-backlog dependency (ADR-022 D7) — **pixels
  must not be enabled for any EU-facing storefront until that feature ships.** Non-EU stores
  are unaffected by this constraint.
- **Server-side Conversions APIs** (Meta CAPI, TikTok Events API, etc.) — named follow-up;
  v1 ships client-side snippets only. This is also why the thank-you-token confidentiality
  fix (PIX:P1) is GA4-only today — the other four providers' fix is the server-side API,
  not a client-side one.
- **PayPal/other non-ECB currency additions beyond the ~30 seeded ECB currencies** — a
  super-admin can add any additional currency manually later; no additional currencies are
  auto-seeded at launch.
- **A batch/digest chat job** (`chat_store_digest`) — named as a later enhancement on the
  existing CLI-runner batch-cost infrastructure; not part of this interactive-chat release.
- **Markdown/HTML rendering of chat assistant output** — the widget renders `textContent`
  only by design; changing this requires a new Safety Agent pass (model output can carry
  attacker-influenced text from product titles or policy pages).

See `RELEASE_CHECKLIST.md` for full evidence per checklist item, `KNOWN_RISKS.md` for the
consolidated risk list (headlined by the pixels GDPR/EU-launch constraint), and
`ROLLBACK_PLAN.md` for the rollback procedure per area.
