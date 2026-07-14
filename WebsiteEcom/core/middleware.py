"""
Core middleware for Pradize.

HostResolutionMiddleware resolves request.store from the Host header once per
request (ADR-001 §4).  Views and templates rely on request.store being set;
it is None for super-admin requests and unrecognized hosts.
"""

from django.db.models import Q


class HostResolutionMiddleware:
    """
    Resolves request.store from the Host header.

    Lookup order:
    1. Subdomain match: Host prefix (everything before the first dot) vs Store.subdomain.
    2. Custom domain match: full Host (port stripped) vs Store.custom_domain.

    Sets request.store = None for:
    - Super-admin requests (no matching store).
    - Unrecognized hosts.
    - Misconfigured hosts that match more than one active store (fail-safe).

    Only active (non-soft-deleted) stores are considered.
    select_related('organization') is fetched in the same query to avoid
    a second DB hit in views that need the organization grouping.
    """

    def __init__(self, get_response):
        self._get_response = get_response

    def __call__(self, request):
        host = request.get_host().split(":")[0].lower()
        from stores.models import Store

        try:
            store = (
                Store.objects.active()
                .filter(Q(subdomain=host.split(".")[0]) | Q(custom_domain=host))
                .select_related("organization")
                .get()
            )
        except Store.DoesNotExist:
            store = None
        except Store.MultipleObjectsReturned:
            store = None  # misconfigured; fail safe
        request.store = store
        return self._get_response(request)
