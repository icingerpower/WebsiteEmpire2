"""
RateSource registry (ADR-023 §1) — same connector-plugin shape as
payments/processor.py's PaymentProcessor ABC and storefront/slots.py's
SlotProvider: callers hold a RateSource and invoke it without knowing whether
it is manual, ECB, or a future connector.

ManualRateSource is the permanent fallback: it is always is_enabled(), and its
run() is a no-op (the admin edits CurrencyRate.rate_to_reference directly
through the admin form — there is nothing to fetch). This is what makes "no
rate source configured" impossible.

EcbRateSource fetches the European Central Bank's free, no-auth daily
reference-rate feed using ONLY the Python standard library
(urllib.request + xml.etree.ElementTree) — zero new dependencies, per ADR-023
§1. run() returns a RateFetchResult with a PER-CURRENCY ok/error outcome
(§IV/§VII design-pattern-ideas: explicit sub-state, no all-or-nothing masking
of partial provider failure) — it does NOT apply any sanity-bound check or
write to the database; that is currency/services.py::apply_fetched_rates's job
(kept separate so EcbRateSource.parse() can be unit-tested against a fixture
with zero database access, per ADR-023 §1 "Tests required").
"""

from __future__ import annotations

import logging
import urllib.request
import xml.etree.ElementTree as ET
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation

logger = logging.getLogger(__name__)

# ECB's stable, free, no-auth daily XML endpoint (ADR-023 §1).
ECB_FEED_URL = "https://www.ecb.europa.eu/stats/eurofxref/eurofxref-daily.xml"

# The ECB feed's XML namespace.
_ECB_NAMESPACE = {"ecb": "http://www.ecb.europa.eu/vocabulary/2002-08-01/eurofxref"}

# Sentinel currency "code" used to report a whole-feed-level parse failure
# (malformed XML, or well-formed XML with no <Cube time="..."> element) —
# never a real ISO 4217 code, so it can never collide with an actual currency
# outcome in the results dict.
FEED_LEVEL_ERROR_KEY = "__feed__"


@dataclass
class CurrencyRateResult:
    """Outcome for a single currency within a RateFetchResult (ADR-023 §1 §IV/§VII)."""

    ok: bool
    rate: Decimal | None = None
    error: str = ""


@dataclass
class RateFetchResult:
    """Per-currency-code -> CurrencyRateResult map returned by RateSource.run()."""

    results: dict[str, CurrencyRateResult] = field(default_factory=dict)


@dataclass
class HealthResult:
    healthy: bool
    latency_ms: float = 0.0
    error_message: str = ""


class RateSource(ABC):
    """Abstract base for all rate-source connectors (ADR-023 §1)."""

    key: str = ""
    name: str = ""
    required_settings: list[str] = []

    def is_enabled(self) -> bool:
        raise NotImplementedError

    def validate_settings(self) -> list[str]:
        """Launch-checklist-style messages. Empty list = fully configured."""
        return []

    @abstractmethod
    def run(self) -> RateFetchResult:
        """Fetch (or no-op) and return a per-currency RateFetchResult."""

    def health_check(self) -> HealthResult:
        return HealthResult(healthy=True)


class ManualRateSource(RateSource):
    """
    The permanent, always-available fallback (ADR-023 §1). There is nothing to
    fetch — the super-admin edits CurrencyRate.rate_to_reference directly.
    """

    key = "manual"
    name = "Manual entry"
    required_settings: list[str] = []

    def is_enabled(self) -> bool:
        return True

    def run(self) -> RateFetchResult:
        return RateFetchResult(results={})


class EcbRateSource(RateSource):
    """European Central Bank daily reference-rate feed (ADR-023 §1)."""

    key = "ecb"
    name = "European Central Bank daily reference rates"
    required_settings: list[str] = []

    def is_enabled(self) -> bool:
        from currency.models import CurrencyConverterSettings

        return CurrencyConverterSettings.get_solo().auto_refresh_enabled

    def run(self) -> RateFetchResult:
        """Fetch the live ECB feed and parse it. Network errors propagate to the
        caller (currency/tasks.py::refresh_currency_rates), which logs at ERROR
        and leaves all rows untouched (ADR-023 §1 "Risks")."""
        xml_bytes = self._fetch()
        return self.parse(xml_bytes)

    @staticmethod
    def _fetch() -> bytes:
        with urllib.request.urlopen(ECB_FEED_URL, timeout=10) as response:  # noqa: S310 (fixed https URL)
            return response.read()

    @staticmethod
    def parse(xml_bytes: bytes) -> RateFetchResult:
        """
        Parse ECB daily-feed XML into a RateFetchResult. Pure function — no
        network, no database — so it is fully unit-testable against a fixture
        (ADR-023 §1 "Tests required": well-formed, malformed, empty, missing
        currency).
        """
        try:
            root = ET.fromstring(xml_bytes)
        except ET.ParseError as exc:
            return RateFetchResult(
                results={FEED_LEVEL_ERROR_KEY: CurrencyRateResult(ok=False, error=f"Malformed XML: {exc}")}
            )

        cube_time = root.find(".//ecb:Cube[@time]", _ECB_NAMESPACE)
        if cube_time is None:
            return RateFetchResult(
                results={FEED_LEVEL_ERROR_KEY: CurrencyRateResult(ok=False, error="No rate data in feed")}
            )

        results: dict[str, CurrencyRateResult] = {}
        for cube in cube_time.findall("ecb:Cube", _ECB_NAMESPACE):
            code = cube.get("currency")
            rate_str = cube.get("rate")
            if not code or not rate_str:
                continue
            try:
                rate = Decimal(rate_str)
            except InvalidOperation:
                results[code] = CurrencyRateResult(ok=False, error=f"Invalid rate value {rate_str!r}")
                continue
            if rate <= 0:
                results[code] = CurrencyRateResult(ok=False, error="Rate is zero or negative")
                continue
            results[code] = CurrencyRateResult(ok=True, rate=rate)

        return RateFetchResult(results=results)
