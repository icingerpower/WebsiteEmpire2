"""
Customer identity service.

Guest checkout + optional account creation post-purchase.
Customer row is auto-created/merged by order email (DECIDED: email-keyed identity,
ADR-001 §3, TICKET-011).

Cross-store rule: all queries go through .for_store(store) — a customer email that
exists on Store A is a completely separate identity from the same email on Store B.
"""

from django.db import transaction

from .models import Customer


@transaction.atomic
def get_or_create_customer(
    store,
    email: str,
    first_name: str = '',
    last_name: str = '',
    phone: str = '',
) -> tuple[Customer, bool]:
    """
    Returns (customer, created). Normalizes email to lowercase+stripped.

    If the customer already exists for this store, only fills in fields that
    are currently blank — existing data is never overwritten. This preserves
    data entered by the customer in a previous checkout or account page.

    The atomic decorator ensures that a concurrent checkout for the same email
    on the same store either finds the existing row or creates exactly one new
    row — never two.
    """
    email = email.lower().strip()
    customer, created = Customer.objects.for_store(store).get_or_create(
        store=store,
        email=email,
        defaults={
            'first_name': first_name,
            'last_name': last_name,
            'phone': phone,
        },
    )
    if not created:
        # Fill in missing fields only — never overwrite existing data.
        changed = False
        if not customer.first_name and first_name:
            customer.first_name = first_name
            changed = True
        if not customer.last_name and last_name:
            customer.last_name = last_name
            changed = True
        if not customer.phone and phone:
            customer.phone = phone
            changed = True
        if changed:
            customer.save(update_fields=['first_name', 'last_name', 'phone', 'updated_at'])
    return customer, created
