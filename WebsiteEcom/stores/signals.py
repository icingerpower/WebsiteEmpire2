"""
Stores app signals.

update_store_employee_last_login:
    Fires on every successful login (django.contrib.auth.signals.user_logged_in).
    Updates StoreEmployee.last_login_at for store-admin users so the super-admin
    dashboard can show when each employee last accessed a store.

    The handler is a no-op when:
    - The user is not a store admin (is_store_admin=False).
    - request.store is None (super-admin surface, or HostResolutionMiddleware
      did not resolve a store — e.g. during Django test client.login() calls
      that do not go through the full middleware stack).
    - No StoreEmployee row exists for (user, store) — graceful skip.

    Uses .update() (single SQL UPDATE) rather than .save() to avoid
    touching unrelated fields and to prevent triggering further signals.
"""

from django.contrib.auth.signals import user_logged_in
from django.db.models.signals import post_save
from django.dispatch import receiver
from django.utils import timezone


@receiver(user_logged_in)
def update_store_employee_last_login(sender, request, user, **kwargs):
    """Update last_login_at for store employees on login to a store surface."""
    if not getattr(user, "is_store_admin", False):
        return
    store = getattr(request, "store", None)
    if store is None:
        return
    # Import here to avoid circular imports during app registry setup.
    from stores.models import StoreEmployee

    StoreEmployee.objects.filter(user=user, store=store).update(
        last_login_at=timezone.now()
    )


@receiver(post_save, sender="stores.StoreLanguage")
def sync_primary_language(sender, instance, **kwargs):
    """
    Keep Store.primary_language in sync when the default StoreLanguage changes (ADR-008 §4).

    Uses .update() (single SQL UPDATE, no save signal, no updated_at touch) so that
    this sync is surgical and does not cascade further signals.

    The field is a denormalised legacy field — new code must read StoreLanguage directly.
    This signal keeps it consistent during the settlement period while legacy readers exist.
    """
    if instance.is_default:
        # Import here to avoid circular imports (stores.models loads before signals).
        from stores.models import Store

        Store.objects.filter(pk=instance.store_id).update(
            primary_language=instance.lang_code
        )
