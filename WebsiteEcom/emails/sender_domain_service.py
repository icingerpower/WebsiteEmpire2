"""
SenderDomain verification orchestration (TICKET-040, ADR-034 "Verification flow").

run_verification_check() is the single place that turns a raw DNS check result
into a SenderDomain state transition. It is called by:
- emails.tasks.verify_sender_domain — one-off check triggered by the store
  owner's "Verify" admin action (synchronous in tests via
  CELERY_TASK_ALWAYS_EAGER, webecom/settings/test.py).
- emails.tasks.recheck_pending_sender_domains — beat task scanning all
  PENDING rows across every store.
- emails.tasks.recheck_verified_sender_domains — weekly beat task scanning
  all VERIFIED rows (domain-takeover protection).

Budget (ADR-034): up to MAX_VERIFICATION_ATTEMPTS attempts over
MAX_VERIFICATION_WINDOW — DNS propagation is slow (24-48h typical), so a
domain whose records simply haven't propagated yet is not failed prematurely.
An explicit content MISMATCH (a record is present but wrong) fails immediately:
retrying will not fix a deliberately-wrong value the way waiting fixes
propagation delay.

Network/timeout errors NEVER change status (design-pattern lesson §XV-1) —
they are logged to last_check_error and the row is left exactly as it was for
the next scheduled attempt.
"""

import logging
from datetime import timedelta

from django.conf import settings
from django.utils import timezone

from . import dns_verification
from .dns_verification import DnsCheckResult
from .models import SenderDomainStatus

logger = logging.getLogger(__name__)

MAX_VERIFICATION_ATTEMPTS = 30
MAX_VERIFICATION_WINDOW = timedelta(days=3)


def run_verification_check(domain, now=None) -> None:
    """
    Run one DNS verification pass for `domain` and persist the result.

    `domain` may be PENDING (normal verification / retry flow) or VERIFIED
    (weekly re-check, demotion path) — never UNVERIFIED or FAILED, since
    those states are only ever entered by an explicit admin action, not by
    this scheduled check.
    """
    now = now or timezone.now()
    was_verified = domain.status == SenderDomainStatus.VERIFIED

    platform_host = getattr(settings, "SENDER_DOMAIN_PLATFORM_HOST", "mail.pradize.com")
    spf_result, spf_detail = dns_verification.check_spf(domain.domain, platform_host)
    dkim_result, dkim_detail = dns_verification.check_dkim(
        domain.expected_dkim_record_name, domain.dkim_public_key
    )

    domain.last_checked_at = now
    domain.verification_attempts += 1

    if DnsCheckResult.ERROR in (spf_result, dkim_result):
        # Transient network/timeout error — never flip status (§XV-1).
        domain.last_check_error = (
            f"DNS lookup error (will retry): spf={spf_detail}; dkim={dkim_detail}"
        )
        domain.save()
        return

    if spf_result == DnsCheckResult.MATCH and dkim_result == DnsCheckResult.MATCH:
        if was_verified:
            # Weekly re-check confirms records are still in place — VERIFIED
            # is not a legal self-transition (_LEGAL_TRANSITIONS), and there
            # is nothing to transition: just persist the housekeeping fields.
            domain.last_check_error = ""
            domain.save()
        else:
            domain.mark_verified()
        return

    if was_verified:
        # Weekly re-check of an already-VERIFIED row: ANY non-match (absent or
        # mismatch) demotes immediately — a domain that stops matching after
        # verification has lost platform trust (domain takeover / DNS change,
        # ADR-034 "Risks") and must not stay silently VERIFIED.
        reason = f"Re-check failed: spf={spf_detail}; dkim={dkim_detail}"
        logger.warning(
            "SenderDomain %s (store=%s) demoted VERIFIED->FAILED: %s",
            domain.domain, domain.store_id, reason,
        )
        domain.mark_failed(reason)
        return

    if DnsCheckResult.MISMATCH in (spf_result, dkim_result):
        domain.mark_failed(f"spf={spf_detail}; dkim={dkim_detail}")
        return

    # Both ABSENT (or one ABSENT + one MATCH) — still propagating. Only fail
    # once the attempt/time budget is exhausted; otherwise stay PENDING.
    budget_exhausted = (
        domain.verification_attempts >= MAX_VERIFICATION_ATTEMPTS
        or (domain.pending_since is not None and now - domain.pending_since >= MAX_VERIFICATION_WINDOW)
    )
    detail = f"Not yet found: spf={spf_detail}; dkim={dkim_detail}"
    if budget_exhausted:
        domain.mark_failed(f"Verification budget exhausted. {detail}")
    else:
        domain.last_check_error = detail
        domain.save()
