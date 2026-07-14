"""
Celery tasks for the reviews app (TICKET-023).

send_pending_review_requests — daily task (scheduled in CELERY_BEAT_SCHEDULE)
that sends review-request emails for all ReviewRequest rows whose scheduled_at
has passed and which have not yet been sent.
"""

from celery import shared_task


@shared_task
def send_pending_review_requests():
    """
    Send review-request emails for all unsent ReviewRequest rows whose
    scheduled_at has passed.

    For each pending row:
    - Sends a 'review_request' transactional email via emails.service.
    - On success, marks is_sent=True and records sent_at.
    - On failure, the row remains unsent and will be retried on the next run.

    Called by Celery beat daily at 08:00 UTC (see CELERY_BEAT_SCHEDULE in base.py).
    """
    from django.db import transaction
    from django.utils import timezone

    from emails.service import send_transactional_email
    from reviews.models import ReviewRequest

    # Snapshot eligible PKs outside any lock so we don't hold a table-wide lock
    # while building the candidate set.  The per-row atomic claim below ensures
    # each row is processed by exactly one worker even when beat overlap occurs.
    pks = list(
        ReviewRequest.objects.cross_store_unsafe()
        .filter(is_sent=False, scheduled_at__lte=timezone.now())
        .values_list("pk", flat=True)
    )

    for pk in pks:
        with transaction.atomic():
            # Claim the row atomically.  skip_locked=True means a second overlapping
            # worker simply skips this row instead of waiting — preventing double-send.
            claimed = (
                ReviewRequest.objects.cross_store_unsafe()
                .filter(pk=pk, is_sent=False)
                .select_for_update(skip_locked=True)
                .select_related("order", "order__store")
                .first()
            )
            if claimed is None:
                # Another worker claimed or already sent this row.
                continue

            order = claimed.order
            ok = send_transactional_email(
                template_id="review_request",
                recipient=order.customer_email,
                context={"order": order},
                store=order.store,
                order=order,
            )
            if ok:
                claimed.is_sent = True
                claimed.sent_at = timezone.now()
                claimed.save(update_fields=["is_sent", "sent_at"])
