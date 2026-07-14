"""
Launch-readiness warning for catalog feeds (ADR-026 D8).

Feeds are NEVER launch-blocking (spec 06 §10: every feed field is "Required
for launch? no"). The one warning-level check ADR-026 D8 asks for: a provider
is enabled but every one of its targets has been stuck error/pending for more
than 24 hours (i.e. it has never successfully generated recently, or at all).

No platform-wide launch-checklist aggregator exists yet in this codebase to
plug this into (ASSUMPTION, low risk) — get_launch_warnings() is exposed as a
standalone, store-scoped function so it can be surfaced today directly on the
feeds admin card (feeds/admin.py) and wired into a future centralized
launch-readiness system without changing its signature.
"""

from datetime import timedelta

from django.utils import timezone

from feeds.models import FeedConfig, FeedTarget, FeedTargetStatus
from feeds.registry import all_providers

_STUCK_HOURS = 24
_STUCK_STATUSES = (FeedTargetStatus.ERROR, FeedTargetStatus.PENDING)


def get_launch_warnings(store) -> list[str]:
    """
    Return human-readable warnings for `store` (empty = nothing to flag).

    A provider is flagged when: it is enabled, has at least one FeedTarget
    row, EVERY row is currently error/pending, AND none of them has
    successfully generated in the last _STUCK_HOURS hours.
    """
    warnings: list[str] = []
    cutoff = timezone.now() - timedelta(hours=_STUCK_HOURS)

    enabled_provider_keys = set(
        FeedConfig.objects.for_store(store).filter(is_enabled=True).values_list("provider", flat=True)
    )

    for provider in all_providers():
        if provider.key not in enabled_provider_keys:
            continue

        targets = list(FeedTarget.objects.for_store(store).filter(provider=provider.key))
        if not targets:
            continue

        all_stuck = all(t.status in _STUCK_STATUSES for t in targets)
        none_generated_recently = all(
            t.last_generated_at is None or t.last_generated_at < cutoff for t in targets
        )
        if all_stuck and none_generated_recently:
            warnings.append(
                f"{provider.name}: all feed targets are error/pending and none has "
                f"generated successfully in over {_STUCK_HOURS} hours."
            )

    return warnings
