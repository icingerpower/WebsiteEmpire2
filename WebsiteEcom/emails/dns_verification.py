"""
DNS TXT lookups for SenderDomain verification (TICKET-040, ADR-034 Option B1).

`dnspython` (requirements.txt — approved 2026-07-11 per ADR-034 "Options
considered" B1) is imported at module load time inside a guarded try/except,
mirroring chat/anthropic_client.py's pattern for the `anthropic` package: never
assume the dependency is present at import time. `_resolve_txt()` is the single
DNS-call boundary — every test patches `emails.dns_verification._resolve_txt`
to simulate present/absent/mismatched/timeout responses. Zero real network I/O
in CI (ADR-034 "Tests required" #3).

All lookups are timeout-bounded via settings.SENDER_DOMAIN_DNS_TIMEOUT_SECONDS
so a slow or black-holed resolver can never hang a Celery worker.

Result classification (ADR-034 "Verification flow"):
- MATCH    the expected record is present with the expected content.
- ABSENT   no record of the relevant type exists yet — DNS propagation delay,
           or the owner simply hasn't published it. NOT a failure by itself;
           the caller (emails/sender_domain_service.py) keeps retrying until
           the attempt/time budget is exhausted.
- MISMATCH a record of the relevant type exists but its content is wrong.
           Treated as an explicit, immediate failure signal ("FAILED (budget
           exhausted OR explicit record mismatch)") — a wrong value published
           today will not fix itself by waiting, unlike propagation delay.
- ERROR    the DNS query itself failed (timeout/network error). Design-pattern
           lesson §XV-1: a transient resolver failure must never be read as
           "verification failed" — the caller must retry and must NEVER change
           SenderDomain.status on ERROR.
"""

import logging

from django.conf import settings

logger = logging.getLogger(__name__)

try:
    import dns.exception
    import dns.resolver
except ImportError:  # pragma: no cover — exercised by test_dns_verification_import.py
    dns = None


class DnsCheckResult:
    MATCH = "match"
    ABSENT = "absent"
    MISMATCH = "mismatch"
    ERROR = "error"


def _get_resolver():
    """Return a configured dns.resolver.Resolver, raising if dnspython is absent."""
    if dns is None:
        raise RuntimeError(
            "dnspython is not installed — required for SenderDomain DNS "
            "verification (requirements.txt, approved 2026-07-11, ADR-034)."
        )
    resolver = dns.resolver.Resolver()
    timeout = getattr(settings, "SENDER_DOMAIN_DNS_TIMEOUT_SECONDS", 5)
    resolver.timeout = timeout
    resolver.lifetime = timeout
    return resolver


def _resolve_txt(hostname: str) -> list:
    """
    Return every TXT record string published at `hostname`.

    This is the single DNS-call boundary that tests patch. Raises the
    underlying dnspython exception (dns.resolver.NXDOMAIN, dns.resolver.NoAnswer,
    dns.exception.Timeout, or any other dns.exception.DNSException subclass) —
    callers classify these into ABSENT vs ERROR; nothing here swallows an
    exception silently (Test Integrity rule).
    """
    resolver = _get_resolver()
    answers = resolver.resolve(hostname, "TXT")
    records = []
    for rdata in answers:
        # A TXT rdata's `.strings` is a tuple of byte chunks — dnspython (and
        # DNS itself) splits long TXT values across multiple <=255-byte
        # strings within one record. Join them before decoding, or a long SPF
        # merge / DKIM key silently truncates to its first chunk.
        chunks = [chunk.decode() if isinstance(chunk, bytes) else str(chunk) for chunk in rdata.strings]
        records.append("".join(chunks))
    return records


def _classify_dns_error(exc: Exception, hostname: str):
    """Translate a raised DNS exception into (DnsCheckResult, detail)."""
    if dns is not None and isinstance(exc, dns.resolver.NXDOMAIN):
        return DnsCheckResult.ABSENT, f"No DNS records found for {hostname}"
    if dns is not None and isinstance(exc, dns.resolver.NoAnswer):
        return DnsCheckResult.ABSENT, f"No TXT records found for {hostname}"
    logger.warning("DNS lookup error for %s: %s", hostname, exc)
    return DnsCheckResult.ERROR, str(exc)


def check_spf(domain: str, expected_include: str):
    """
    Check the domain apex for an SPF TXT record containing `expected_include`
    (e.g. "mail.pradize.com") in its `include:` mechanism.

    Returns (DnsCheckResult, detail_message).
    """
    try:
        records = _resolve_txt(domain)
    except Exception as exc:
        return _classify_dns_error(exc, domain)

    spf_records = [r for r in records if r.strip().lower().startswith("v=spf1")]
    if not spf_records:
        return DnsCheckResult.ABSENT, f"No SPF (v=spf1) TXT record found at {domain}"

    for record in spf_records:
        if expected_include.lower() in record.lower():
            return DnsCheckResult.MATCH, record

    return (
        DnsCheckResult.MISMATCH,
        f"SPF record found but missing 'include:{expected_include}': {spf_records[0]}",
    )


def check_dkim(selector_hostname: str, expected_public_key: str):
    """
    Check '<selector>._domainkey.<domain>' for a DKIM TXT record whose 'p='
    tag contains `expected_public_key` exactly.

    Returns (DnsCheckResult, detail_message).
    """
    try:
        records = _resolve_txt(selector_hostname)
    except Exception as exc:
        return _classify_dns_error(exc, selector_hostname)

    dkim_records = [r for r in records if "v=dkim1" in r.strip().lower()]
    if not dkim_records:
        return DnsCheckResult.ABSENT, f"No DKIM (v=DKIM1) TXT record found at {selector_hostname}"

    if expected_public_key:
        for record in dkim_records:
            if expected_public_key in record:
                return DnsCheckResult.MATCH, record

    return (
        DnsCheckResult.MISMATCH,
        f"DKIM record found but public key does not match: {dkim_records[0]}",
    )
