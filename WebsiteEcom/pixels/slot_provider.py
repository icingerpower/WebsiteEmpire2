"""
PixelsSlotProvider — the single bridge between pixels.Pixel rows and the
existing storefront.slots 'pixels' slot (ADR-022 D3).

Rejected alternative (ADR-022 D3): five separate SlotProviders, one per
provider. That costs five is_enabled() store-settings queries per request
and duplicates the row-fetch logic; this bridge keeps per-request cost at
exactly ONE query (Pixel.objects.for_store(store).filter(is_active=True)).

Registered into storefront.slots by PixelsConfig.ready() importing this
module (register() call at the bottom) — mirrors every other registry in
this codebase.
"""

import logging

from django.utils.safestring import mark_safe

from pixels.models import Pixel
from pixels.registry import get_provider
from storefront.slots import SlotProvider, register

logger = logging.getLogger(__name__)


def _resolve_consent(request):
    """
    Resolve the shopper's consent state (ADR-025 D3), imported locally to
    avoid an app-loading-order import cycle between pixels and consent (both
    self-register into storefront.slots at ready() time).

    Routed through consent.state.resolve_consent_for_pixels() rather than
    get_consent() directly (RM-1 fix, KNOWN_RISKS.md item 1 / ADR-025 D3
    dated correction 2026-07-11): a store with consent disabled AND the
    non-EU acknowledgment signed (ConsentSettings.is_enabled=False,
    non_eu_acknowledged=True, D6) must render pixels unconditionally — no
    shopper on that store is ever shown the banner, so gating on the cookie
    alone (get_consent()) left pixels permanently dark. See that function's
    docstring for the full resolution rule; this is the single place both
    this render-path gate and the claim_purchase_pixels() caller
    (storefront/views_checkout.py) get it from.

    Rollback shim (ADR-025 "Rollback strategy"): if the `consent` app/package
    is ever removed from the codebase entirely, this import raises
    ImportError — caught here and replaced with a locally defined,
    independent fail-closed stand-in (never importing anything from the
    `consent` package in the except branch) so a botched rollback dark-
    launches pixels rather than firing them unconsented (§XV-1).
    """
    try:
        from consent.state import resolve_consent_for_pixels
    except ImportError:
        from dataclasses import dataclass

        @dataclass(frozen=True)
        class _FailClosedConsent:
            def allows(self, category: str) -> bool:
                return category == "necessary"

        return _FailClosedConsent()
    return resolve_consent_for_pixels(request)


class PixelsSlotProvider(SlotProvider):
    """
    Renders every installed+active Pixel row's base snippet plus any matching
    canonical-event snippets for the current render (ADR-022 D3/D5/D6).

    Context contract (explicit, never sniffed — ADR-022 D5):
      pixel_events: list[tuple[str, dict]] — (canonical_event, payload) pairs
          the calling view adds explicitly (e.g. product_page adds
          ('view_content', payload); checkout_view adds ('initiate_checkout',
          payload) only after its own conditional-UPDATE claim succeeds;
          order_thank_you adds ('purchase', payload) whenever the order is
          PAID).
      purchase_pixel_providers: set[str] — provider keys for which THIS
          render's claim_purchase_pixels() call returned created=True
          (ADR-022 D6). A 'purchase' entry in pixel_events is rendered for a
          given row ONLY when that row's provider key is in this set —
          created=False (reload, back button) renders nothing, by
          construction, even if the payload is present.

    Both keys are optional; their absence means "no event render this
    request" (e.g. every page except product/checkout/thank-you), not an
    error.
    """

    slot = "pixels"
    key = "pixels"
    name = "Pixel integrations"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        """
        Always True — gating lives per-row (is_active) inside render(), which
        already performs the slot's one-and-only query. A second query here
        just to decide is_enabled would double the per-request cost the
        bridge exists to avoid (ADR-022 D3).
        """
        return True

    def render(self, context) -> str:
        request = context.get("request")
        store = getattr(request, "store", None)
        if store is None:
            return ""

        # ONE query (ADR-022 D3) — every row's base snippet plus any matching
        # event snippets are rendered from this single fetch.
        rows = list(Pixel.objects.for_store(store).filter(is_active=True))
        if not rows:
            return ""

        # ADR-025 D3: server-side render gating by consent category. A row
        # renders (base snippet AND any matching event snippets) ONLY when
        # its provider's fixed consent_category is currently allowed —
        # undecided/refused renders nothing for that row, no inert markup.
        consent = _resolve_consent(request)
        consent_mode = {
            "analytics_granted": consent.allows("analytics"),
            "marketing_granted": consent.allows("marketing"),
        }

        pixel_events = context.get("pixel_events") or []
        purchase_pixel_providers = context.get("purchase_pixel_providers") or set()
        # PIX:P1 (human decision 2026-07-11): an optional normalized, tokenless
        # URL set explicitly by a view (currently only order_thank_you) so the
        # automatic page_view never reports the real URL — which, on the
        # thank-you page, contains the never-expiring signed order token.
        # None everywhere else (unchanged behaviour). See PixelProvider.
        # render_base() docstring (pixels/registry.py) for which providers
        # actually honor it.
        page_url_override = context.get("page_url_override")

        parts = []
        for row in rows:
            provider = get_provider(row.provider)
            if provider is None:
                # Row exists but the code plugin was removed — never emit a
                # broken tag (§XV-1). validate_settings() surfaces this in
                # the launch checklist; here we just log loudly and skip.
                logger.error(
                    "Pixel row store=%s provider=%r has no matching registry "
                    "entry; rendering nothing for this row.",
                    store.pk, row.provider,
                )
                continue
            if not consent.allows(provider.consent_category):
                # Undecided/refused for this row's category — render nothing
                # at all for it this request (ADR-025 D3). The purchase
                # claim itself is gated separately, at the claim site
                # (pixels.service.claim_purchase_pixels), not here (D0).
                continue
            try:
                parts.append(
                    provider.render_base(
                        row.pixel_id, page_url=page_url_override, consent_mode=consent_mode,
                    )
                )
                for canonical_event, payload in pixel_events:
                    if canonical_event == "purchase" and row.provider not in purchase_pixel_providers:
                        # created=False (reload/back-button) or this provider
                        # wasn't installed at claim time — render nothing.
                        continue
                    snippet = provider.render_event(row.pixel_id, canonical_event, payload)
                    if snippet:
                        parts.append(snippet)
            except Exception:
                # Isolate this row's failure from the others — render_slot()
                # already isolates provider-level failures, this is a second
                # layer so one misconfigured row never takes down the rest.
                logger.exception(
                    "PixelProvider %r raised while rendering for store=%s; "
                    "skipping this row.",
                    row.provider, store.pk,
                )
        return mark_safe("".join(parts))

    def validate_settings(self, store) -> list:
        """
        Launch-checklist errors (ADR-022 D8): one per active row whose
        pixel_id fails its provider's regex, or whose provider key has no
        registry entry. No rows / all inactive -> [] (pixels are optional).
        """
        errors = []
        for row in Pixel.objects.for_store(store).filter(is_active=True):
            provider = get_provider(row.provider)
            if provider is None:
                errors.append(
                    f"Pixel provider '{row.provider}' is configured but not recognized "
                    "by the running code (plugin removed?)."
                )
                continue
            errors.extend(provider.validate_settings(row))
        return errors


register(PixelsSlotProvider())
