"""
Store employee invite flow (ADR-033 D4b, AF-108, TICKET-047).

resolve_invitee(email) — existing-user link vs new-user creation.
find_conflicting_employee(email, store) — pre-check used by the invite form
to surface a friendly "already a member" error (T047 report gap fix, see its
own docstring).
send_store_invite_email(employee, is_new_user, request) — the ONE call site
for the "Store invitation" transactional email, via emails.service.
send_transactional_email (never a direct send_mail call — single email
entry point, per emails/service.py's own docstring convention).
"""

from django.core.exceptions import ValidationError
from django.urls import reverse

from core.models import User
from stores.tokens import build_invite_token

STORE_INVITATION_TEMPLATE_ID = "store_invitation"


def find_conflicting_employee(email, store):
    """
    Return the existing StoreEmployee row if `email` already belongs to an
    active-or-inactive employee of `store`, else None.

    T047 report gap: `user` is not a field on the store-site invite form
    (replaced by full_name/email/phone, resolved server-side in
    StoreEmployeeStoreAdminAdmin.save_model), so Django's automatic
    validate_unique() never sees it and never runs the (user, store)
    UniqueConstraint check. Without this pre-check, inviting an email that
    is already an employee of the store reaches obj.save() and hits the DB
    constraint directly — an uncaught IntegrityError (HTTP 500) instead of a
    form error. Called from StoreEmployeeStoreAdminForm.clean() BEFORE
    resolve_invitee() runs, so the duplicate is caught before any write.
    """
    email = (email or "").strip().lower()
    if not email:
        return None
    from stores.models import StoreEmployee

    return (
        StoreEmployee.objects.filter(user__email__iexact=email, store=store)
        .select_related("user")
        .first()
    )


def resolve_invitee(email):
    """
    Return (user, created).

    Email matches an existing User -> link it (no duplicate account, AF-108).
    No match -> create User(email=..., is_store_admin=True) with an unusable
    password (StoreEmployeeStoreAdminAdmin.save_model sends the invite email
    afterwards; accepted_at is stamped only when this new user completes the
    set-password link — stores.views.accept_invite_view).
    """
    email = (email or "").strip().lower()
    if not email:
        raise ValidationError("Email is required to invite an employee.")

    try:
        user = User.objects.get(email__iexact=email)
        if not user.is_store_admin:
            user.is_store_admin = True
            user.save(update_fields=["is_store_admin"])
        return user, False
    except User.DoesNotExist:
        pass

    # AbstractUser.username is still required (REQUIRED_FIELDS) and unique;
    # using the email as the username is a reasonable default (ASSUMPTION,
    # LOW) — Django's default username validator allows '@' and '.'.
    user = User(username=email, email=email, is_store_admin=True)
    user.set_unusable_password()
    user.save()
    return user, True


def send_store_invite_email(employee, is_new_user, request=None):
    """
    Send the "Store invitation" transactional email (AF-108).

    New users get a set-password link (stamp-once accepted_at happens when
    they complete it, stores/views.py accept_invite_view). Existing users
    already have credentials — they get a plain access-granted notice and
    accepted_at is stamped immediately (PENDING/ASSUMPTION, LOW: no separate
    "accept" ritual makes sense for an account that can already log in).
    """
    from emails.service import send_transactional_email

    user = employee.user
    store = employee.store
    context = {
        "invited_name": employee.invited_name or user.email,
        "accept_url": None,
    }

    if is_new_user:
        uidb64, token = build_invite_token(user)
        path = reverse(
            "stores_accept_invite",
            kwargs={"uidb64": uidb64, "employee_pk": employee.pk, "token": token},
        )
        context["accept_url"] = request.build_absolute_uri(path) if request else path
        subject = f"You're invited to join {store.name}"
    else:
        from django.utils import timezone

        if employee.accepted_at is None:
            employee.accepted_at = timezone.now()
            employee.save(update_fields=["accepted_at"])
        subject = f"You now have access to {store.name}"

    send_transactional_email(
        template_id=STORE_INVITATION_TEMPLATE_ID,
        recipient=user.email,
        context=context,
        store=store,
        subject_override=subject,
    )
