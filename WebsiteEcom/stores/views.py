"""
Public (unauthenticated) views for the stores app.

accept_invite_view — the set-password landing page for the employee invite
flow (ADR-033 D4b, AF-108, TICKET-047). Reached via the "Store invitation"
email's link; never requires login (the invitee has no usable password yet).
"""

from django.contrib.auth.forms import SetPasswordForm
from django.shortcuts import redirect, render
from django.utils import timezone

from core.models import User
from stores.models import StoreEmployee
from stores.tokens import decode_uidb64, invite_token_generator


def accept_invite_view(request, uidb64, employee_pk, token):
    """
    GET  — render the set-password form if the token is valid.
    POST — set the password, stamp StoreEmployee.accepted_at ONCE (never
           re-stamped afterwards — same stamp-once audit convention as
           chat/admin.py's subprocessor_terms_accepted_at), redirect to the
           store admin login page.

    An invalid/expired/already-used token (PasswordResetTokenGenerator's hash
    includes the user's password, so it stops validating the instant a real
    password is set — no separate single-use table needed) renders a plain
    "this link is no longer valid" page instead of raising.
    """
    user = None
    employee = None
    user_pk = decode_uidb64(uidb64)
    if user_pk is not None:
        try:
            user = User.objects.get(pk=user_pk)
            employee = StoreEmployee.objects.get(pk=employee_pk, user=user)
        except (User.DoesNotExist, StoreEmployee.DoesNotExist):
            user = None
            employee = None

    valid = user is not None and invite_token_generator.check_token(user, token)

    if not valid:
        return render(request, "stores/accept_invite_invalid.html", status=400)

    if request.method == "POST":
        form = SetPasswordForm(user, request.POST)
        if form.is_valid():
            form.save()
            if employee.accepted_at is None:
                employee.accepted_at = timezone.now()
                employee.save(update_fields=["accepted_at"])
            return redirect("/admin/login/")
    else:
        form = SetPasswordForm(user)

    return render(
        request,
        "stores/accept_invite.html",
        {"form": form, "employee": employee, "store": employee.store},
    )
