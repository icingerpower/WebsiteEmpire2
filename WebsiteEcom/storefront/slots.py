"""
Slot registry for the Pradize storefront (ADR-012, D9, TICKET-029 Phase 1).

The slot registry is the plugin pattern for all storefront integrations that inject
HTML at named injection points (e.g. pixel scripts, consent banners, overlays).

SLOT NAMES
  Declared P1 slots (empty wrappers shipped; providers land in integration tickets):
    consent       — consent manager gate (TH-141; gates the 'pixels' slot)
    pixels        — purchase/marketing pixels (T030)
    overlay       — modal overlays (T034)
    social_proof  — social proof widgets (T035)
    security_badge
    bump_product / bump_cart / bump_floating_cart  — order-bump placements (T027)
    bump_checkout — order bump injected in the checkout order summary (PA-004, ADR-015 §8 Phase 3).
                    Rendered between the summary items and the payment section.
                    Providers must suppress themselves when checkout_state.step != 'payment'.
    thankyou_funnel  — post-purchase upsell widget (T028)
    amazon_buy    — Amazon buy-button (T036)
    reviews       — product review section + card stars (T023)
    chat_launcher — AI sales-assistant chat widget (ADR-024, TICKET-038, spec 15 TH-045)

USAGE
    # Integration ticket registers a provider at app ready() time:
    from storefront.slots import register
    register(MyPixelProvider())

    # Template renders the slot:
    {% render_slot "pixels" %}
    # → <div data-slot="pixels">…rendered providers…</div>

PROVIDER CONTRACT
    class SlotProvider:
        slot: str               # slot name this provider renders into
        key: str                # unique key for dedup/logging
        name: str               # human-readable name (admin UI)
        required_settings: list[str]  # setting names that must be non-empty when enabled

        def is_enabled(self, store) -> bool:
            # Return True if this provider should render for the given store.
            # May query StoreSettings rows — must be cheap (called on every request).

        def render(self, context) -> str:
            # Return safe HTML. Never raise — return '' on error and log.

        def validate_settings(self, store) -> list[str]:
            # Return human-readable error messages for the launch checklist.
            # Called by the settings-validation architecture (L* items), not per-request.
"""

import logging

logger = logging.getLogger(__name__)

# _registry maps slot_name → list of SlotProvider instances (in registration order).
_registry: dict[str, list] = {}


class SlotProvider:
    """
    Base class / protocol for slot providers (ADR-012 D9).

    Subclass this and implement the abstract methods, then call register(instance)
    in your app's ready() method.
    """

    #: Slot this provider renders into (e.g. "pixels").
    slot: str = ""
    #: Unique identifier for this provider (used in dedup and logging).
    key: str = ""
    #: Human-readable name displayed in the admin launch checklist.
    name: str = ""
    #: Django setting names that must be non-empty for this provider to work.
    required_settings: list[str] = []

    def is_enabled(self, store) -> bool:
        """Return True if this provider should render for the given store."""
        raise NotImplementedError(f"{self.__class__.__name__}.is_enabled() not implemented.")

    def render(self, context) -> str:
        """
        Render and return safe HTML for injection into the slot wrapper.
        Must not raise — return '' on error and log.
        """
        raise NotImplementedError(f"{self.__class__.__name__}.render() not implemented.")

    def validate_settings(self, store) -> list[str]:
        """
        Return a list of human-readable validation error messages for the launch checklist.
        An empty list means the provider is fully configured.
        Called by the settings-validation architecture, not per-request.
        """
        return []


def register(provider: SlotProvider) -> None:
    """
    Register a SlotProvider into the slot registry.

    Call this from your app's AppConfig.ready() method so it runs after Django's
    app registry is fully populated.

    Args:
        provider: A SlotProvider instance.  provider.slot must be non-empty.
    """
    if not provider.slot:
        raise ValueError(
            f"SlotProvider {provider!r} has an empty 'slot' attribute. "
            "Set provider.slot to the target slot name before registering."
        )
    _registry.setdefault(provider.slot, []).append(provider)
    logger.debug("Registered slot provider %r into slot %r.", provider.key or repr(provider), provider.slot)


def get_providers(slot_name: str) -> list:
    """
    Return the list of SlotProvider instances registered for the given slot.

    Returns an empty list if no providers are registered for that slot — this is
    normal for P1 (providers ship in integration tickets).
    """
    return list(_registry.get(slot_name, []))
