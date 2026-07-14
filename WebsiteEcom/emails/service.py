"""
Transactional email service (TICKET-022).

send_transactional_email() is the single entry point for all email sending.
It never raises — it catches exceptions, logs them to SentEmail, and returns
False on failure.  Callers get a reliable bool and the audit trail is preserved.

Template resolution order:
  1. EmailTemplate row for (store, template_id) where is_active=True.
  2. File-based default in emails/templates/emails/<template_id>.html/.txt.

Subject rendering: always rendered as a Django template string so variables
like {{ store.name }} and {{ order.order_number }} work in store overrides.

None → '' sanitization: _sanitize_context() recursively replaces None with ''
in plain dict/list structures before rendering.  Django model objects are left
untouched — their attributes are resolved by the template engine at access time.
This satisfies AC-183: no raw 'None' string appears in rendered output.

Sender address (TICKET-040, ADR-034): both send paths resolve `from_email` via
emails.sender.resolve_from_email(store) — the single resolver that returns a
verified custom per-store address when one exists, or the platform default
otherwise. Never inline `settings.DEFAULT_FROM_EMAIL` here again.
"""

import logging

from django.core.mail import send_mail
from django.db import transaction
from django.template import engines
from django.template.loader import render_to_string

from emails.sender import resolve_from_email

logger = logging.getLogger(__name__)


def _serialize_campaign_context(context: dict) -> dict:
    """
    Replace all Django model instances and QuerySets with safe plain-scalar
    equivalents before rendering store-authored campaign templates.

    This prevents a store-admin from writing a template like:
        {{ store.organization.processor_accounts.all.0.api_secret }}
    and receiving sensitive data (Stripe/PayPal secrets) in an email.

    Conversion rules:
    - 'store'        → {'name': str, 'subdomain': str}
    - 'cart_items'   → list of {'name': str, 'quantity': int, 'price': str}
    - 'discount_code'→ the code string only (not the model instance)
    - 'resume_url', 'coupon_code', and other plain scalars → unchanged
    - Any other key whose value is a Model instance or QuerySet → dropped with
      a warning log entry so the problem is observable without crashing.

    The function is intentionally narrow: it only knows the keys used by
    send_campaign_email's callers (campaigns/tasks.py).  Unknown model keys
    are dropped rather than serialized ad-hoc so that additions to the context
    require an explicit serialization rule here.
    """
    from django.db.models import Model, QuerySet

    safe: dict = {}
    for key, value in context.items():
        if key == "store" and isinstance(value, Model):
            safe[key] = {
                "name": str(getattr(value, "name", "")),
                "subdomain": str(getattr(value, "subdomain", "")),
            }
        elif key == "cart_items" and isinstance(value, (list, QuerySet)):
            items = []
            for item in value:
                items.append({
                    "name": str(getattr(item, "variant", "")),
                    "quantity": int(getattr(item, "quantity", 0)),
                    "price": str(getattr(item, "unit_price", "")),
                })
            safe[key] = items
        elif key == "discount_code" and isinstance(value, Model):
            # Only expose the code string, never the full model object.
            safe[key] = str(getattr(value, "code", ""))
        elif isinstance(value, (Model, QuerySet)):
            logger.warning(
                "_serialize_campaign_context: dropping context key %r "
                "(Model/QuerySet not allowed in campaign email context — "
                "add an explicit serialization rule for this key)",
                key,
            )
        else:
            # Plain scalars (str, int, Decimal, bool, None, list of scalars)
            # are passed through unchanged.
            safe[key] = value
    return safe

# Default subjects rendered as Django template strings.
# Override per-store via EmailTemplate rows.
_DEFAULT_SUBJECTS = {
    'order_confirmation': 'Your order #{{ order.order_number }} is confirmed',
    'order_shipped': 'Your order #{{ order.order_number }} has shipped',
    'welcome': 'Welcome to {{ store.name }}',
    'password_reset': 'Reset your {{ store.name }} password',
    'review_request': 'How was your {{ store.name }} order?',
    # ADR-018 D3: contact-form notification to full-access store employees.
    'contact_message_received': 'New contact message on {{ store.name }}',
    # ADR-027 D3: double opt-in confirmation for lead-capture signups on
    # DE-targeting stores.
    'lead_capture_confirm': 'Please confirm your subscription to {{ store.name }}',
    # ADR-029 D7: automated gift-card campaign delivery (platform default —
    # a campaign's own email_subject, when non-blank, overrides this).
    'gift_card_campaign': 'Here is your gift card code',
}


def _sanitize_context(ctx):
    """
    Recursively replace None with '' in plain dict/list structures.

    Django model instances are left untouched — their attribute access is
    handled by the template engine.  Only plain Python scalars are converted,
    ensuring AC-183 compliance: no raw 'None' string in rendered output.
    """
    if isinstance(ctx, dict):
        return {k: _sanitize_context(v) for k, v in ctx.items()}
    if isinstance(ctx, list):
        return [_sanitize_context(v) for v in ctx]
    if ctx is None:
        return ''
    return ctx


def _render_subject(subject_template: str, context: dict) -> str:
    """Render a subject string template with the given context."""
    engine = engines['django']
    return engine.from_string(subject_template).render(context).strip()


def _render_from_db_template(email_template, context: dict):
    """
    Render subject, HTML body, and plain-text body from a store EmailTemplate.

    Returns (subject, body_html, body_text).  body_text may be '' when not set.
    """
    engine = engines['django']
    subject = engine.from_string(email_template.subject).render(context).strip()
    body_html = engine.from_string(email_template.body_html).render(context)
    body_text = ''
    if email_template.body_text:
        body_text = engine.from_string(email_template.body_text).render(context)
    return subject, body_html, body_text


def _render_from_file_template(template_id: str, context: dict):
    """
    Render subject, HTML body, and plain-text body from file-based templates.

    Returns (subject, body_html, body_text).  body_text may be '' if no .txt
    template exists for this template_id.

    TemplateDoesNotExist is allowed to propagate so the caller can log it.
    """
    default_subject = _DEFAULT_SUBJECTS.get(
        template_id,
        'A message from {{ store.name }}',
    )
    subject = _render_subject(default_subject, context)
    body_html = render_to_string(f'emails/{template_id}.html', context)
    try:
        body_text = render_to_string(f'emails/{template_id}.txt', context)
    except Exception:
        body_text = ''
    return subject, body_html, body_text


def send_transactional_email(
    template_id: str,
    recipient: str,
    context: dict,
    store,
    order=None,
    subject_override: str = '',
    body_html_override: str = '',
) -> bool:
    """
    Render and send a transactional email for a store.

    Args:
        template_id: Stable string id (e.g. 'order_confirmation').
        recipient:   Recipient email address.
        context:     Template variables dict. None values in plain dicts/lists
                     are replaced with '' before rendering (AC-183).
        store:       Store instance — used for template lookup and branding.
        order:       Optional Order instance for audit logging.
        subject_override: When non-blank, rendered as a Django template string
                     and used INSTEAD of the resolved EmailTemplate/file subject
                     (ADR-029 D7 — per-campaign delivery override, resolved in
                     this one place rather than a parallel send path). Blank
                     falls through to the normal precedence chain.
        body_html_override: Same override behavior as subject_override, for the
                     HTML body. When set, body_text is left blank (the override
                     is HTML-only, matching the admin campaign editor's single
                     rich-text field).

    Returns:
        True on success (send_mail accepted by backend), False on any failure.
        Never raises — all exceptions are caught and logged.

    Side effects:
        - Writes a SentEmail audit row on every attempt (success or failure).
    """
    from emails.models import SentEmail, SentEmailStatus

    # Always include the store in context for subject/body rendering.
    full_context = dict(context)
    full_context.setdefault('store', store)
    full_context = _sanitize_context(full_context)

    subject = ''
    body_html = ''
    body_text = ''
    status = SentEmailStatus.SENT
    error_message = ''

    try:
        # 1. Try store-specific override.
        from emails.models import EmailTemplate
        store_template = (
            EmailTemplate.objects.for_store(store)
            .filter(template_id=template_id, is_active=True)
            .first()
        )
        if store_template:
            subject, body_html, body_text = _render_from_db_template(
                store_template, full_context
            )
        else:
            # 2. Fall back to file-based platform default.
            subject, body_html, body_text = _render_from_file_template(
                template_id, full_context
            )

        # 3. Per-field campaign override (ADR-029 D7) — applied AFTER the
        # normal precedence chain above so a blank override field still falls
        # through to the store/platform template it was compared against.
        if subject_override:
            subject = _render_subject(subject_override, full_context)
        if body_html_override:
            engine = engines['django']
            body_html = engine.from_string(body_html_override).render(full_context)
            body_text = ''

        from_email = resolve_from_email(store)

        send_mail(
            subject=subject,
            message=body_text or '',
            from_email=from_email,
            recipient_list=[recipient],
            html_message=body_html or None,
            fail_silently=False,
        )

    except Exception as exc:
        logger.exception(
            "Failed to send %s email to %s for store %s: %s",
            template_id,
            recipient[:3] + "***",
            getattr(store, 'pk', store),
            exc,
        )
        status = SentEmailStatus.FAILED
        error_message = str(exc)

    # Write audit row regardless of success or failure.
    try:
        SentEmail.objects.create(
            store=store,
            template_id=template_id,
            recipient_email=recipient,
            subject=subject,
            order=order,
            status=status,
            error_message=error_message,
        )
    except Exception as log_exc:
        # Never let audit logging failure propagate — just log it.
        logger.error(
            "Could not write SentEmail audit row for %s to %s: %s",
            template_id,
            recipient[:3] + "***",
            log_exc,
        )

    return status == SentEmailStatus.SENT


def send_campaign_email(
    subject_template: str,
    body_template: str,
    recipient_email: str,
    context: dict,
    store,
) -> bool:
    """
    Render and send a campaign email (e.g. abandoned-checkout sequence emails).

    Unlike send_transactional_email(), the subject and body templates are
    provided directly by the caller (store-authored content from
    AbandonedCheckoutEmailStep.subject / .body_template) rather than resolved
    from EmailTemplate rows or file-based templates. This is the only path for
    campaign emails — never create a parallel sender (§XV-4 single send path).

    Reuses _sanitize_context, _render_subject, and the SentEmail write pattern
    from send_transactional_email to keep a consistent audit trail.

    Args:
        subject_template:  Django template string for the subject line.
        body_template:     Django template string for the HTML body.
        recipient_email:   Recipient email address.
        context:           Template variables dict. None values in plain dicts/lists
                           are replaced with '' before rendering (AC-183 compliance).
        store:             Store instance — used for branding context and audit.

    Returns:
        True on success (send_mail accepted by backend), False on any failure.
        Never raises — all exceptions are caught and logged.

    Side effects:
        - Writes a SentEmail audit row on every attempt (template_id='abandoned_checkout_email').
    """
    from emails.models import SentEmail, SentEmailStatus

    _TEMPLATE_ID = "abandoned_checkout_email"

    full_context = dict(context)
    full_context.setdefault("store", store)
    # Serialize model instances to plain dicts before rendering store-authored
    # templates.  This prevents template traversal attacks where a store-admin
    # writes {{ store.organization.processor_accounts.all.0.api_secret }} to
    # exfiltrate payment processor credentials (Safety B1).
    full_context = _serialize_campaign_context(full_context)
    # AC-183: replace any remaining None values with '' in plain structures.
    full_context = _sanitize_context(full_context)

    subject = ""
    body_html = ""
    status = SentEmailStatus.SENT
    error_message = ""

    try:
        subject = _render_subject(subject_template, full_context)
        engine = engines["django"]
        body_html = engine.from_string(body_template).render(full_context)

        from_email = resolve_from_email(store)

        send_mail(
            subject=subject,
            message="",
            from_email=from_email,
            recipient_list=[recipient_email],
            html_message=body_html or None,
            fail_silently=False,
        )

    except Exception as exc:
        logger.exception(
            "Failed to send campaign email to %s for store %s: %s",
            recipient_email[:3] + "***",
            getattr(store, "pk", store),
            exc,
        )
        status = SentEmailStatus.FAILED
        error_message = str(exc)

    # Write audit row regardless of success or failure.
    # Isolated in its own savepoint: if the create() fails inside an outer
    # transaction.atomic() block (e.g. from campaigns/tasks.py), only this
    # savepoint is rolled back — the outer transaction continues cleanly.
    try:
        with transaction.atomic():
            SentEmail.objects.create(
                store=store,
                template_id=_TEMPLATE_ID,
                recipient_email=recipient_email,
                subject=subject,
                status=status,
                error_message=error_message,
            )
    except Exception as log_exc:
        # Never let audit logging failure propagate — just log it.
        logger.error(
            "Could not write SentEmail audit row for campaign email to %s: %s",
            recipient_email[:3] + "***",
            log_exc,
        )

    return status == SentEmailStatus.SENT
