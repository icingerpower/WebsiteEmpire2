"""
Celery tasks for transactional email (TICKET-022) and per-store sender-domain
DNS verification (TICKET-040, ADR-034).

send_email_async is a thin wrapper around send_transactional_email() for use
from non-signal contexts (e.g., scheduled abandoned-cart jobs, campaign emails).

Context design: the context dict must be JSON-serializable (Celery serializes
task arguments as JSON). Django model instances must NOT be included — pass
store_id and order_id as separate arguments; the task resolves the objects.

Retry policy: max 3 retries with exponential back-off.  The underlying service
never raises, so retries are driven by explicit self.retry() calls when the
service returns False.

SenderDomain tasks (ADR-034 "Verification flow"):
- verify_sender_domain: one-off check triggered by the store owner's "Verify"
  admin action (emails/admin.py). Synchronous in tests via
  CELERY_TASK_ALWAYS_EAGER (webecom/settings/test.py).
- recheck_pending_sender_domains: beat task, re-checks every PENDING row
  across all stores (registered in CELERY_BEAT_SCHEDULE, webecom/settings/base.py).
- recheck_verified_sender_domains: weekly beat task, re-checks every VERIFIED
  row and demotes to FAILED if DNS records have disappeared.
All three delegate to emails.sender_domain_service.run_verification_check(),
which is the one place that classifies a DNS check result into a state
transition — never duplicate that logic here.
"""

import logging

from celery import shared_task

logger = logging.getLogger(__name__)


@shared_task(name='emails.tasks.send_email_async', bind=True, max_retries=3)
def send_email_async(self, template_id, recipient, context, store_id, order_id=None):
    """
    Send a transactional email asynchronously.

    Args:
        template_id: Stable email type identifier (e.g. 'order_confirmation').
        recipient:   Recipient email address string.
        context:     JSON-safe dict of template variables. Must not contain
                     Django model instances (pass IDs and resolve here).
        store_id:    Store PK — resolved to a Store instance before rendering.
        order_id:    Optional Order PK for audit logging.

    The task resolves Store (and optionally Order) from their PKs, adds the
    store to the context, then delegates to send_transactional_email().

    On failure (service returns False), retries with exponential back-off up to
    max_retries times.  After exhausting retries, the failure is logged and the
    task is marked as failed (no further retry).
    """
    from emails.service import send_transactional_email
    from stores.models import Store

    try:
        store = Store.objects.get(pk=store_id)
    except Store.DoesNotExist:
        logger.error(
            "send_email_async: Store %s not found — dropping email %s to %s",
            store_id,
            template_id,
            recipient,
        )
        return False

    order = None
    if order_id is not None:
        try:
            from orders.models import Order
            order = Order.objects.cross_store_unsafe().get(pk=order_id)
        except Exception:
            logger.warning(
                "send_email_async: Order %s not found — sending %s without order link",
                order_id,
                template_id,
            )

    success = send_transactional_email(
        template_id=template_id,
        recipient=recipient,
        context=dict(context),
        store=store,
        order=order,
    )

    if not success:
        try:
            raise self.retry(
                countdown=2 ** self.request.retries * 60,
                exc=RuntimeError(
                    f"send_transactional_email returned False for {template_id} to {recipient}"
                ),
            )
        except self.MaxRetriesExceededError:
            logger.error(
                "send_email_async: Max retries exceeded for %s to %s (store %s)",
                template_id,
                recipient,
                store_id,
            )

    return success


# ---------------------------------------------------------------------------
# SenderDomain DNS verification (TICKET-040, ADR-034)
# ---------------------------------------------------------------------------


@shared_task(name='emails.tasks.verify_sender_domain', bind=True, max_retries=3)
def verify_sender_domain(self, domain_id):
    """
    Run one DNS verification pass for a single SenderDomain.

    Triggered by the store owner's "Verify" admin action after
    SenderDomain.mark_pending() has already been called. Not retried on a
    normal ABSENT/MISMATCH outcome — those are legitimate PENDING/FAILED
    results, not task failures. Retries only apply to unexpected exceptions
    escaping run_verification_check() (it is designed not to raise on DNS
    errors, but infrastructure errors — e.g. DB unavailable — should still
    use Celery's retry machinery like every other task in this module).
    """
    from emails.models import SenderDomain
    from emails.sender_domain_service import run_verification_check

    try:
        domain = SenderDomain.objects.get(pk=domain_id)
    except SenderDomain.DoesNotExist:
        logger.error("verify_sender_domain: SenderDomain %s not found", domain_id)
        return

    try:
        run_verification_check(domain)
    except Exception as exc:
        try:
            raise self.retry(countdown=2 ** self.request.retries * 60, exc=exc)
        except self.MaxRetriesExceededError:
            logger.exception(
                "verify_sender_domain: giving up on domain %s after max retries", domain_id
            )


@shared_task(name='emails.tasks.recheck_pending_sender_domains')
def recheck_pending_sender_domains():
    """
    Beat task: re-check every PENDING SenderDomain across all stores.

    Deliberately cross-store — SenderDomain is a plain Model (ADR-034 Option
    A), not StoreOwnedModel, for exactly this reason (mirrors
    stores.StoreDomain / ADR-008). A failure on one domain must not abort the
    scan for the rest.
    """
    from emails.models import SenderDomain, SenderDomainStatus
    from emails.sender_domain_service import run_verification_check

    for domain in SenderDomain.objects.filter(status=SenderDomainStatus.PENDING):
        try:
            run_verification_check(domain)
        except Exception:
            logger.exception(
                "recheck_pending_sender_domains: check failed for domain id=%s", domain.pk
            )


@shared_task(name='emails.tasks.recheck_verified_sender_domains')
def recheck_verified_sender_domains():
    """
    Weekly beat task: re-check every VERIFIED SenderDomain and demote to
    FAILED if its DNS records have disappeared (domain-takeover / DNS-change
    protection — ADR-034 "Risks": a store could lose control of a domain
    after being marked VERIFIED). Cadence is flagged PENDING product decision
    in ADR-034; weekly is the documented default from TICKET-040's blockers.
    """
    from emails.models import SenderDomain, SenderDomainStatus
    from emails.sender_domain_service import run_verification_check

    for domain in SenderDomain.objects.filter(status=SenderDomainStatus.VERIFIED):
        try:
            run_verification_check(domain)
        except Exception:
            logger.exception(
                "recheck_verified_sender_domains: check failed for domain id=%s", domain.pk
            )
