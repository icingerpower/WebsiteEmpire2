"""
FeedProvider registry (ADR-026 D1) — mirrors pixels/registry.py exactly.

Concrete providers (feeds/providers.py) self-register into the module-level
_registry dict at import time (FeedsConfig.ready() imports feeds.providers,
which calls register() once per provider at module scope). Adding a third
provider (Pinterest — ADR-026 PENDING P-1) means one FeedProvider subclass +
one register() call — never a call-site edit (design-pattern-ideas §IX).

Frozen keys (ADR-026 D1): must equal FeedConfig.provider / FeedTarget.provider
choices (feeds.models.FeedProviderChoices) AND the URL segment
(/feeds/<provider>/...) — pinned by feeds/tests/test_registry.py.
"""

import logging

logger = logging.getLogger(__name__)

# key -> FeedProvider instance, in registration order.
_registry: dict[str, "FeedProvider"] = {}


class FeedProvider:
    """
    Base class for one catalog-feed platform integration (ADR-026 D1).

    Both v1 providers share the renderer (feeds/renderer.py) and the item
    builder (feeds/items.py); per-provider deltas are expressed entirely as
    the `field_map` class attribute (DATA, not code) — see feeds/providers.py.
    """

    #: Frozen registry key — must equal FeedConfig/FeedTarget.provider and the
    #: /feeds/<key>/... URL segment.
    key: str = ""
    #: Human-readable name for the admin card title.
    name: str = ""
    #: [] in v1 — feeds need no store-supplied credentials (the feed URL is
    #: *given to* the platform, not the reverse). Declared anyway so the
    #: settings-validation architecture sees this connector like every other.
    required_settings: list[str] = []
    #: Ordered list of (shared item dict key, XML tag) pairs this provider
    #: emits. Both v1 providers share every row except g:identifier_exists
    #: (Google only) — feeds/providers.py._SHARED_FIELD_MAP.
    field_map: list[tuple[str, str]] = []

    def validate_settings(self, feed_config) -> list[str]:
        """
        Return human-readable errors for the launch checklist (ADR-026 D8).

        Only rule in v1: scope='collections' requires at least one collection
        selected — a FeedConfig in that state would silently produce a feed
        with zero candidate products (indistinguishable from "generation
        broke"), so it is flagged instead (§XV-1).
        """
        from feeds.models import FeedScope

        errors = []
        if feed_config.scope == FeedScope.COLLECTIONS and not feed_config.collections.exists():
            errors.append(
                f"{self.name}: scope is 'Selected collections' but no collections are selected."
            )
        return errors

    def is_enabled(self, store) -> bool:
        """True when this store has an enabled FeedConfig row for this provider."""
        from feeds.models import FeedConfig

        return FeedConfig.objects.for_store(store).filter(
            provider=self.key, is_enabled=True
        ).exists()

    def run(self, target) -> None:
        """
        Generate one FeedTarget's file. Called by feeds/tasks.py ONLY — never
        by feeds/views.py (ADR-026 D1: the public view is a thin token check
        + FileResponse, generation never happens in-request).
        """
        from feeds.tasks import _process_target

        _process_target(target)

    def health_check(self, store) -> dict:
        """
        Worst status across this provider's FeedTarget rows for `store`
        (ADR-026 D1). Surfaces on the admin dashboard per §XV-1: any ERROR
        target turns the whole provider card red with its reasons.
        """
        from feeds.models import FeedTarget, FeedTargetStatus

        targets = list(
            FeedTarget.objects.for_store(store)
            .filter(provider=self.key)
            .select_related("store_language")
        )
        if not targets:
            return {"status": "unknown", "reasons": ["No feed targets configured yet."]}

        error_targets = [t for t in targets if t.status == FeedTargetStatus.ERROR]
        if error_targets:
            reasons = [
                f"{t.store_language.lang_code}-{t.country_code}: {t.last_error}"
                for t in error_targets
            ]
            return {"status": "error", "reasons": reasons}
        return {"status": "ok", "reasons": []}

    def item_attributes(self, item: dict) -> list[tuple[str, object]]:
        """
        Return this provider's (xml_tag, value) pairs for one shared item dict,
        in field_map order, omitting keys whose value is empty/None/[] (D4:
        "Sale price ... only when set", "brand ... omitted when blank", etc.
        — every optional-field omission rule collapses to this one check).

        `additional_image_links` maps to a list value; feeds/renderer.py
        writes one <tag> element per list entry (multiple g:additional_image_link
        tags per item, as both platforms expect).
        """
        pairs = []
        for key, tag in self.field_map:
            value = item.get(key)
            if value in (None, "", []):
                continue
            pairs.append((tag, value))
        return pairs


def register(provider: FeedProvider) -> None:
    """Register a FeedProvider instance. Call once at module import time."""
    if not provider.key:
        raise ValueError(f"FeedProvider {provider!r} has an empty 'key' attribute.")
    _registry[provider.key] = provider
    logger.debug("Registered feed provider %r.", provider.key)


def get_provider(key: str) -> "FeedProvider | None":
    """Return the registered FeedProvider for key, or None if not registered."""
    return _registry.get(key)


def all_providers() -> list["FeedProvider"]:
    """Return all registered providers, in registration order."""
    return list(_registry.values())
