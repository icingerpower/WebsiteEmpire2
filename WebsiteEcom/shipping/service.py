"""
Shipping zone resolution service.

Resolution rule (most-specific-first):
  - A zone with explicit country_codes containing the buyer's country code wins.
  - If no such zone has active rates for this store, fall back to catch-all zones
    (zones with country_codes=[]).
  - Results are sorted by rate_sort_key() (SM-003): price ascending, then
    estimated_days_max ascending (None last), then name — the single sort used
    everywhere a rate list needs to be ordered (ShippingForm consumes this
    already-sorted list rather than re-sorting).

Implementation note on country_codes filtering:
  `JSONField.__contains__` is PostgreSQL-only in Django.  To stay compatible with
  SQLite (dev/test) and PostgreSQL (production) without conditional branching in
  every environment, zone resolution fetches all active zones and filters them
  in Python.  Zones are a small, rarely-changing, platform-level dataset (expected
  N < 100) so the extra fetch is negligible.
"""

from __future__ import annotations

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from stores.models import Store

# Sentinel used when a rate has no estimated_days_max — sorts it after every
# rate that *does* quote an estimate (SM-003 tie-break, ADR-015 §5 SM-002).
_NO_ESTIMATE_SENTINEL = 10**9


def rate_sort_key(rate):
    """
    Single sort key shared by resolve_shipping_rates() and ShippingForm
    (SM-003) — rates must never be sorted by price alone: two rates at the
    same price previously sorted in unstable/arbitrary order (whatever order
    the DB query happened to return), which could flip the "cheapest" pick
    on SM-002 pre-selection between requests. The tie-break is:
      1. price (ascending — cheaper first)
      2. estimated_days_max (ascending — faster first; None sorts last)
      3. name (ascending — deterministic final tie-break)
    """
    return (
        rate.price,
        rate.estimated_days_max if rate.estimated_days_max is not None else _NO_ESTIMATE_SENTINEL,
        rate.name,
    )


def resolve_shipping_rates(store: "Store", country_code: str, weight_grams: int = 0) -> list:
    """
    Return applicable ShippingRate instances for a buyer in country_code.

    Resolution: most-specific zone (explicit country_codes list containing the
    buyer's country) wins over catch-all zone (empty country_codes).
    If no explicit-country zone has rates for this store, fall through to catch-all.

    Returns a list of active ShippingRate objects sorted by price ascending.
    weight_grams is accepted for future WEIGHT_BASED filtering but does not
    currently filter out non-matching weight-based rates (reserved for Phase 2).
    """
    from shipping.models import ShippingRate, ShippingZone

    # Fetch all active zones and split into specific vs. catch-all in Python.
    # This avoids JSONField.__contains__ which is PostgreSQL-only in Django;
    # zones are a small platform-level dataset so the Python loop is fine.
    all_active_zones = list(ShippingZone.objects.filter(is_active=True))

    specific_zone_ids = [z.id for z in all_active_zones if country_code in z.country_codes]
    catch_all_zone_ids = [z.id for z in all_active_zones if not z.country_codes]

    rates_qs = (
        ShippingRate.objects.for_store(store)
        .filter(is_active=True, zone__is_active=True)
        .select_related("zone", "carrier")
    )

    if specific_zone_ids:
        specific_rates = list(rates_qs.filter(zone_id__in=specific_zone_ids))
        if specific_rates:
            return sorted(specific_rates, key=rate_sort_key)

    # Fall through to catch-all zones (country_codes=[])
    catch_all_rates = list(rates_qs.filter(zone_id__in=catch_all_zone_ids))
    return sorted(catch_all_rates, key=rate_sort_key)
