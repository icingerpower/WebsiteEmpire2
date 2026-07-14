"""
resolve_from_email(store) — the single sender-address resolution function
(TICKET-040, ADR-034 "Sending path change", design-pattern lesson §XV-4:
shared resources need a single resolution function).

Both emails/service.py send paths (send_transactional_email,
send_campaign_email) call this instead of inlining
`getattr(settings, 'DEFAULT_FROM_EMAIL', ...)` — the two identical call sites
this ticket replaces.
"""

from email.utils import formataddr

from django.conf import settings

# ASSUMPTION (TICKET-040): ADR-034 specifies
#   formataddr((store.email_display_name, f"{local}@{domain}"))
# and "Reply-to is unchanged (still the store's existing Settings-C1 reply-to
# field)". Neither `Store.email_display_name` nor a reply-to field actually
# exist as columns anywhere in this codebase today (grep confirms it — only
# `DEFAULT_FROM_EMAIL` is used platform-wide; Settings-C1's "store sets
# display name + reply-to" v1 behavior was decided but those two Store fields
# were never built in TICKET-022). Resolving that gap is a Settings-C1 follow
# up, not this ticket's scope. Using `store.name` (which does exist) as the
# display name is the simplest reasonable default: no new Store schema change,
# no email routing behavior change (there is no reply-to header anywhere in
# emails/service.py today, so "reply-to unchanged" holds trivially — there is
# nothing to preserve or break). Flagged here for the Architect/Spec agents.
_DISPLAY_NAME_FALLBACK_FIELD = "name"


def resolve_from_email(store) -> str:
    """
    Resolve the envelope From address to use when sending email for `store`.

    - store has a SenderDomain with status=VERIFIED -> a verified custom
      From-address: "<display name> <local@domain>" (ADR-034).
    - otherwise (no SenderDomain row, or not yet VERIFIED) ->
      settings.DEFAULT_FROM_EMAIL — today's exact v1 behavior. Pure additive
      fallback: no regression for any store that never configures a sender
      domain (AC-183).

    settings.SENDER_DOMAINS_ENABLED=False (ADR-034 "Rollback strategy") short-
    circuits straight to the platform default — instant revert, no data
    migration, no impact on any other table.
    """
    if not getattr(settings, "SENDER_DOMAINS_ENABLED", True):
        return settings.DEFAULT_FROM_EMAIL

    # Deferred import: emails.models pulls in payments.fields at module load,
    # and this module must stay import-safe from emails/service.py's own
    # module level without introducing a load-order dependency.
    from emails.models import SenderDomain, SenderDomainStatus

    domain = (
        SenderDomain.objects.filter(store=store, status=SenderDomainStatus.VERIFIED)
        .order_by("-verified_at")
        .first()
    )
    if domain is None:
        return settings.DEFAULT_FROM_EMAIL

    display_name = getattr(store, "email_display_name", "") or getattr(store, _DISPLAY_NAME_FALLBACK_FIELD, "")
    address = f"{domain.sender_local_part}@{domain.domain}"
    return formataddr((display_name, address))
