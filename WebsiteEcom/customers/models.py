"""
Customers app: Customer, CustomerAddress.

Design decisions (spec §4 Customer entity, ADR-001, TICKET-011):
- Email-keyed identity: a Customer row is auto-created (or retrieved) by the order
  email at checkout time via get_or_create_customer() in service.py. This supports
  guest checkout and optional account creation post-purchase without requiring a login.
- email is always stored lowercase and stripped — enforced in save() to guarantee the
  unique_together (store, email) constraint behaves as case-insensitive.
- Denormalized counters (total_orders, total_spent, is_verified_buyer) are incremented
  by the order service; they are NEVER recomputed from the order set on every read.
- anonymized_at records the GDPR anonymization timestamp. The field is present now;
  the anonymization logic (zeroing PII columns) is wired in a later ticket.
- CustomerAddress stores saved shipping/billing addresses for account customers.
  Guest checkouts do not create CustomerAddress rows — their address lives on the
  Order snapshot only.
"""

from django.db import models

from core.models import StoreOwnedModel


class Customer(StoreOwnedModel):
    """
    A customer identity record keyed by (store, email).

    Invariants:
    - email is always lowercase and stripped (enforced in save()).
    - unique_together (store, email) prevents cross-store identity collisions.
    - total_orders and total_spent are denormalized counters; the order service
      increments them atomically — do not recompute from the order set.
    - is_verified_buyer is set True once the customer has at least one paid order.
    - anonymized_at is non-null when PII (name, phone, email body) has been erased
      for GDPR; the erasure logic is wired in a later ticket.
    - tags is a list of strings; used by lead capture campaigns (TICKET-034).
    """

    email = models.EmailField()
    first_name = models.CharField(max_length=255, blank=True, default='')
    last_name = models.CharField(max_length=255, blank=True, default='')
    phone = models.CharField(max_length=50, blank=True, default='')

    tags = models.JSONField(
        default=list,
        help_text="List of string tags; used by lead capture campaigns.",
    )
    notes = models.TextField(blank=True, default='')

    # Denormalized counters — incremented by the order service, never recomputed.
    total_orders = models.PositiveIntegerField(default=0)
    total_spent = models.DecimalField(max_digits=12, decimal_places=2, default=0)

    is_verified_buyer = models.BooleanField(
        default=False,
        help_text="True once the customer has at least one paid order.",
    )
    accepts_marketing = models.BooleanField(default=False)

    # GDPR: set when PII is anonymized. Wired policy comes in a later ticket.
    anonymized_at = models.DateTimeField(
        null=True,
        blank=True,
        help_text="Set when this customer record has been anonymized for GDPR.",
    )

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    def save(self, *args, **kwargs):
        # Always normalize email to lowercase + stripped before persisting.
        self.email = self.email.lower().strip()
        super().save(*args, **kwargs)

    @property
    def full_name(self):
        """
        Returns the customer's full name, or falls back to email if both name
        parts are blank (e.g. a guest customer who never provided a name).
        """
        return f"{self.first_name} {self.last_name}".strip() or self.email

    def __str__(self):
        return self.full_name

    class Meta:
        verbose_name = 'customer'
        verbose_name_plural = 'customers'
        unique_together = [('store', 'email')]
        indexes = [
            models.Index(fields=['store', 'email']),
            models.Index(fields=['store', 'created_at']),
        ]


class CustomerAddress(StoreOwnedModel):
    """
    A saved address for an account customer.

    Guest checkouts do not create CustomerAddress rows — their shipping and
    billing addresses live only in the Order.shipping_address / billing_address
    JSON snapshots.

    is_default marks the address the storefront pre-fills at checkout.
    country stores an ISO 3166-1 alpha-2 code (2-char upper-case string).
    """

    customer = models.ForeignKey(
        Customer,
        on_delete=models.CASCADE,
        related_name='addresses',
    )
    is_default = models.BooleanField(default=False)
    name = models.CharField(max_length=255)
    line1 = models.CharField(max_length=255)
    line2 = models.CharField(max_length=255, blank=True, default='')
    city = models.CharField(max_length=100)
    state = models.CharField(max_length=100, blank=True, default='')
    postal_code = models.CharField(max_length=20)
    country = models.CharField(
        max_length=2,
        help_text="ISO 3166-1 alpha-2 country code, e.g. 'US', 'FR'.",
    )

    def __str__(self):
        return f"{self.name}, {self.city}"

    class Meta:
        verbose_name = 'customer address'
        verbose_name_plural = 'customer addresses'
        indexes = [
            models.Index(fields=['customer']),
        ]
