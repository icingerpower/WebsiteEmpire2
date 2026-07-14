# Rollback Plan — Pixel Integrations + Currency Display + AI Sales Chat

**Date:** 2026-07-11

This release spans three areas that can be rolled back independently — each is its own
Django app (`pixels`, `currency`, `chat`) with no cross-app foreign keys between them, except
that `chat`'s policy-page feature reads (never writes) `pages.StaticPage` rows, and both
`pixels` and `chat` render through the pre-existing `storefront` slot system (ADR-012).

---

## Area 1: Pixel integrations (TICKET-030 / ADR-022)

### What was added

**New app:** `pixels/` — `Pixel` model, `PixelProvider` registry (`pixels/registry.py`, 5
providers), 10 templates (`pixels/templates/pixels/*_base.html`, `*_event.html`),
`PixelsSlotProvider` (`pixels/slot_provider.py`, slot `"pixels"`), admin card UI
(`pixels/templates/admin/pixels/pixel/change_list.html`, `uninstall_confirm.html`),
`pixels/service.py` (purchase-claim logic).

**Touched existing apps:**
- `storefront/views.py` — product pixel context.
- `storefront/views_checkout.py` — `initiate_checkout` ordering fix, thank-you purchase
  claim (`claim_purchase_pixels`), `page_url_override` (PIX:P1 token strip).
- `storefront/templates/storefront/pages/product.html` — AJAX add-to-cart bridge with the
  `response.ok` branch fix.
- `cart/views.py` — `_safe_redirect` hardening (`url_has_allowed_host_and_scheme`).
- `orders/migrations/0013_retire_purchase_guard_sentinel.py` — converts the old
  `__purchase_guard__` sentinel `FiredPixel` rows into per-provider rows.

**Database schema (migrations):**
| App | Migration(s) | Notes |
|---|---|---|
| pixels | `0001_initial` onward | new app — `Pixel`, `FiredPixel` tables |
| orders | `0013_retire_purchase_guard_sentinel` | data migration, has a tested reverse |

### Rollback procedure

1. **Disable at the data layer first (fastest, no code risk).** Set every store's `Pixel.
   is_active = False` (or delete the rows) — `PixelsSlotProvider.render()` queries
   `Pixel.objects.for_store(store).filter(is_active=True)` per request, so this takes effect
   immediately with zero deploy. This is the correct **first response to the EU/GDPR concern**
   too: if a pixel was accidentally enabled for an EU store, disabling the row is instant and
   safe.
2. **If a full code revert is required:** revert `pixels/`, and the touched files in
   `storefront/`, `cart/views.py`, and `orders/migrations/0013_...` via git.
   - **Caution on `orders/migrations/0013`:** its reverse operation recreates one sentinel
     row per order that has any per-provider purchase row, and is idempotent (tested:
     `test_reverse_is_idempotent`). It does **not** restore sentinels for orders that had zero
     active providers at forward-migration time — this is a known, accepted, consequence-free
     gap (documented in `PIXELS_AUDIT.md` §4) because pre-T030 code rendered no pixels for
     those orders at all.
   - **Caution on `cart/views.py _safe_redirect`:** the `url_has_allowed_host_and_scheme` fix
     is a general hardening fix (closes a backslash-URL redirect bypass), not pixels-specific.
     Do not revert it as part of isolating a pixels-only rollback — it protects every caller
     of `_safe_redirect`, including the cart-update/remove flows that existed before this
     release.
3. **Migrations:** `pixels` schema is entirely new (safe to `migrate pixels zero` if the app
   itself is being removed). `orders/migrations/0013` is additive+data; reverse it explicitly
   if going back before it:
   ```bash
   python3 manage.py migrate orders <migration-before-0013>
   python3 manage.py migrate pixels zero
   ```
   Determine the exact `<prev>` from `showmigrations` on the actual deployed environment.
4. **No financial state to worry about.** Pixels are read-only observers of order state
   (`FiredPixel` rows are dedup markers, not money) — rolling back never touches captured
   payments.
5. **GDPR-specific rollback:** if pixels were live on an EU store and must be pulled for
   compliance, prefer step 1 (per-store `is_active=False`) over a full app removal — it is
   instant, reversible, and does not touch `FiredPixel` history (so re-enabling later does not
   re-fire already-claimed Purchase events, per the insert-before-fire design).

---

## Area 2: Currency display (TICKET-031 + TICKET-037 / ADR-023)

### What was added

**New app:** `currency/` — `CurrencyDefinition`, `CurrencyRate`, `StoreCurrencySetting`,
`CurrencyConverterSettings` (singleton), `currency/templatetags/currency_tags.py`
(`display_price`, `currency_conversion_note`, `currency_picker`), `currency/tasks.py`
(`refresh_currency_rates`, gated by `auto_refresh_enabled`), `currency/session.py`
(display-currency resolution).

**Touched existing apps:** none load-bearing — `currency_tags` is added to
`TEMPLATES["OPTIONS"]["builtins"]` in `webecom/settings/base.py` so every template gets the
tags with no explicit `{% load %}`, but no existing template file was rewritten to use them
except the new currency-picker/price-display partials this release adds.

**Database schema (migrations):**
| Migration | Notes |
|---|---|
| `0001_initial` | `CurrencyDefinition`, `CurrencyRate`, `StoreCurrencySetting`, `CurrencyConverterSettings` |
| `0002_seed_ecb_currencies` | data migration, seeds ~30 ECB currencies + EUR anchor |
| `0003_currencydefinition_code_placement_and_more` | adds `show_code` (default `False`), `code_placement` (default `suffix`) — additive, no backfill |

### Rollback procedure

1. **Disable at the settings layer first.** `CurrencyConverterSettings.auto_refresh_enabled`
   is already `False` by default; if the *auto-refresh task* specifically is the problem
   (e.g. a bad ECB response corrupting rates), flip it off via `/superadmin/` — no deploy
   needed, and `currency/tasks.py:31-36`'s docstring confirms the task always checks this
   flag before doing anything, so leaving the beat schedule entry registered is safe even
   with the feature off.
2. **Disable the storefront picker specifically:** set every `StoreCurrencySetting.is_enabled
   = False` for a store (or delete the rows) — `currency_picker` does not render when zero
   currencies are enabled for a store (per the ADR-023 §3 addendum: "zero enabled rows = off,
   picker does not render"). This stops shopper-facing display conversion without touching
   checkout/emails, which never used it in the first place (see the conversion-boundary
   guards below).
3. **If a full code revert is required:**
   ```bash
   python3 manage.py migrate currency zero
   ```
   then remove `"currency"` from `INSTALLED_APPS` and `currency.templatetags.currency_tags`
   from the template `builtins` list in `webecom/settings/base.py`, and revert
   `currency/` via git.
4. **No risk to checkout/order/email correctness.** Verified by this release's own tests
   (`currency/tests/test_checkout_conversion_boundary.py`,
   `currency/tests/test_email_conversion_boundary.py`) that checkout, thank-you, the order
   record, and transactional emails **never** reference the conversion template tags in the
   first place — they are structurally incapable of being affected by a currency-app
   rollback, because they were never wired to it. This was already true before this release
   (ADR-015 ML-004, shipped earlier) and ADR-023 only adds the *display* conversion for
   pre-checkout pages.
5. **Data:** `CurrencyRate` rows are platform-global reference data, not shopper data — no
   customer records are affected by a rollback.

---

## Area 3: AI sales-assistant chat (TICKET-038 / ADR-024)

### What was added

**New app:** `chat/` — `ChatSession`, `ChatMessage`, `StoreChatSettings`, `chat/tools.py`
(4 read-only tools), `chat/anthropic_client.py`, `chat/system_prompt.py`, `chat/sessions.py`
(signed session tokens), `chat/quota.py` (spend/budget enforcement), `chat/admin.py`,
`chat/slot_provider.py` (slot `"chat_launcher"`), `chat/tasks.py` (retention purge),
`chat/views.py` (`/chat/session/`, `/chat/message/`).

**Touched existing apps:**
- `storefront/templates/storefront/partials/chat_widget.html` — new widget partial.
- `storefront/templates/storefront/base.html` — `{% render_slot "chat_launcher" %}`.
- `storefront/templates/storefront/base_checkout.html` — empties the `chat_launcher` block
  (chat-on-checkout suppression, CK-006).
- `storefront/templatetags/storefront_tags.py` — `PREVIEW_SUPPRESSED_SLOTS` gains
  `"chat_launcher"`.
- `storefront/tests/test_theme_conformance.py` (TH-132) — re-verified under the new slot key.
- `webecom/settings/base.py` — `CHAT_*` settings, `CELERY_BEAT_SCHEDULE
  ["purge-expired-chat-sessions"]`.

**Database schema (migrations):** `chat/migrations/0001_initial` onward — entirely new app,
no fields added to any pre-existing model.

### Rollback procedure

1. **`CHAT_KILL_SWITCH` is the intended, instant rollback lever — use it first.** Setting the
   `CHAT_KILL_SWITCH` environment variable (any of `1`/`true`/`yes`) makes
   `_gating_refusal(store)` (`chat/views.py`) refuse both `/chat/session/` and
   `/chat/message/` immediately, for every store, with no deploy or data change. This is the
   documented, tested rollback path (`chat/tests/test_views.py::
   test_session_refused_when_kill_switch_on`) and should be the first response to any
   incident (cost overrun, upstream outage, a discovered prompt-injection issue, etc.).
2. **Per-store disable (narrower):** unchecking `StoreChatSettings.is_enabled` for a specific
   store (via its `/admin/`) disables chat for that store only, without a kill switch or
   deploy. The GDPR acceptance stamp (`subprocessor_terms_accepted_at`) is **preserved**, not
   cleared, on disable (verified:
   `chat/tests/test_admin.py::test_disabling_later_preserves_the_acceptance_stamp`) — so
   re-enabling later does not require re-accepting the sub-processor terms.
3. **If a full code revert is required:**
   ```bash
   python3 manage.py migrate chat zero
   ```
   then remove `"chat"` from `INSTALLED_APPS`, revert `chat/` via git, and revert the
   `chat_launcher` block/slot references in `storefront/`. Do **not** revert
   `storefront/templatetags/storefront_tags.py`'s `PREVIEW_SUPPRESSED_SLOTS` entry for
   `"chat_launcher"` in isolation if the slot itself is kept — an un-suppressed chat launcher
   would then render inside theme preview mode.
4. **Data:** `ChatSession`/`ChatMessage` rows are the GDPR-relevant transcript data
   (30-day retention purge already runs daily via `purge_expired_chat_sessions`). A rollback
   should not bulk-delete existing sessions beyond what the retention task already does —
   removing the app (`migrate chat zero`) does drop the tables, so if there is any reason to
   preserve transcripts (e.g. an open support case), export them via the admin **before**
   running `migrate chat zero`.
5. **Spend/quota state:** `AiJobMetric` rows (`job_type='sales_chat'`) and `StoreAiQuota`
   counters are **not** part of the `chat` app itself (they live in the pre-existing
   `aijobs`/`core` infrastructure) and are unaffected by a `chat`-app-only rollback — historic
   spend accounting is preserved even if `chat` is fully removed.
6. **Anthropic API key:** no rollback action needed — the SDK reads `ANTHROPIC_API_KEY` from
   the environment; removing the app does not require unsetting it (harmless if left set).

---

## Shared surface — `storefront` slot system (ADR-012)

Both pixels (`slot.pixels`) and chat (`slot.chat_launcher`) render through the existing slot
system, and both are listed in `PREVIEW_SUPPRESSED_SLOTS`
(`storefront/templatetags/storefront_tags.py:59`). If rolling back only one of the two areas,
remove only that area's entry from `PREVIEW_SUPPRESSED_SLOTS`, not the whole set — removing
`"pixels"` would let pixels fire inside theme preview mode (already regression-tested against:
`test_render_slot_suppresses_pixels_in_preview_mode`); removing `"chat_launcher"` would let
the chat widget render inside preview mode. `storefront/tests/test_theme_conformance.py`
(TH-132) is the post-rollback smoke check for the slot system generally:
```bash
python3 manage.py test storefront.tests.test_theme_conformance
```

## What is NOT rolled back by this plan

- Real Anthropic API spend already recorded into `AiJobMetric`/`StoreAiQuota` for chat turns
  that already completed — those are historical accounting records, not reversible by a code
  or schema rollback.
- `ChatSession`/`ChatMessage` transcripts, unless explicitly exported before `migrate chat
  zero` is run.
- `FiredPixel` purchase-claim history for pixels — these are dedup markers proving a Purchase
  event already fired for a given (order, provider); removing the `pixels` app drops this
  history, and re-adding the app later will not re-fire duplicate Purchase events for orders
  whose claims already existed (per the insert-before-fire design), but it also means the
  claim history itself is gone, not "undone".
- Currency rate history (`CurrencyRate.previous_rate` single-slot history) — a rollback does
  not restore any rate audit trail beyond what the model itself already keeps.
