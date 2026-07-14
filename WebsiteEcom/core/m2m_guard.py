"""
Write-side guard for M2M fields between two StoreOwnedModel subclasses (ADR-031).

Companion to the read-side relaxation in core/managers.py: relation-bound M2M
reads are safe *only if* no through row ever links two different stores'
objects. This module makes that invariant true by construction, connecting a
`m2m_changed` `pre_add` receiver to every such field's `through` model, so a
cross-store `.add()`/`.set()` raises IsolationError before the through row is
ever written — regardless of call site (admin, service, shell, migration
code that goes through the manager).

`connect_store_m2m_guards()` is called once from `CoreConfig.ready()` and
walks the *entire* app registry (not a hand-maintained list), so a new M2M
field added later between two StoreOwnedModel subclasses is automatically
covered — enforced by
core/tests/test_store_owned_model_compliance.py's guard-registration
introspection test.
"""

from django.apps import apps
from django.db.models.signals import m2m_changed

from core.managers import IsolationError


def _guard(sender, instance, action, reverse, model, pk_set, **kwargs):
    """
    m2m_changed receiver: block a pre_add whose target rows belong to a
    different store than `instance`.

    Only `pre_add` is checked — pre_remove/post_* never create new
    cross-store links (removal only deletes through rows), and pre_clear has
    no pk_set to check.

    Uses `model.objects.cross_store_unsafe()` deliberately: this IS the
    audited, intentional case for reading across all stores (verifying the
    added rows' store_id), not a bypass of the isolation the guard itself
    enforces.
    """
    if action != "pre_add" or not pk_set:
        return
    cross_store = (
        model.objects.cross_store_unsafe()
        .filter(pk__in=pk_set)
        .exclude(store_id=instance.store_id)
    )
    if cross_store.exists():
        raise IsolationError(
            "Cross-store M2M link forbidden (ADR-031): %s -> %s"
            % (instance._meta.label, model._meta.label)
        )


def connect_store_m2m_guards():
    """
    Connect `_guard` to the `through` model of every M2M field whose source
    AND target are both StoreOwnedModel subclasses.

    Walks `local_many_to_many` on every concrete model in the app registry —
    covers all five current M2M-between-StoreOwnedModel fields (DiscountCode.
    product_conditions/collection_conditions, GiftCardCampaign.
    trigger_products, LeadCaptureCampaign.excluded_pages, FeedConfig.
    collections) and any future one without a code change here.

    dispatch_uid is stable per (model, field) so repeated calls (e.g. under
    the test runner's app-registry reloads) never double-connect.
    """
    # Local import: avoids a core.models <-> core.m2m_guard import cycle at
    # module load time (StoreOwnedModel is defined in core.models, which is
    # imported by every app's models.py before AppConfig.ready() runs).
    from core.models import StoreOwnedModel

    for model in apps.get_models():
        if not issubclass(model, StoreOwnedModel) or model._meta.abstract:
            continue
        for field in model._meta.local_many_to_many:
            remote_model = field.remote_field.model
            if isinstance(remote_model, str):
                # Unresolved lazy reference — should not happen once the app
                # registry is fully loaded (ready() runs after all models are
                # loaded), but skip defensively rather than crash startup.
                continue
            if not issubclass(remote_model, StoreOwnedModel):
                continue
            m2m_changed.connect(
                _guard,
                sender=field.remote_field.through,
                dispatch_uid=f"store_m2m_guard.{model._meta.label_lower}.{field.name}",
            )
