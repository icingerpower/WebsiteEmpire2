"""
Core models: User, StoreOwnedModel.

User is the single custom user model shared by both admin sites (ADR-001 §1, §2).
StoreOwnedModel is the abstract base class that every store-scoped model must
extend — no exceptions (ADR-001 §4).  Its StoreScopedManager raises on unscoped
queries to prevent cross-tenant data bleed (§XV-1).
"""

from django.contrib.auth.models import AbstractUser
from django.db import models

from core.managers import _MODEL_VALIDATION_WINDOW, StoreScopedManager


class User(AbstractUser):
    """
    Single user model for both the store-admin (/admin/) and super-admin
    (/superadmin/) surfaces.

    Email is the login identifier (USERNAME_FIELD = 'email').  The username
    field is retained from AbstractUser for display/internal purposes but is
    no longer the auth lookup key.

    is_store_admin: user can access at least one store's /admin/.
                    Actual per-store access is gated by StoreEmployee rows.
    is_super_admin: user can access /superadmin/ (cross-org control plane).
    """

    # Override AbstractUser's email to enforce uniqueness — it is now the
    # login identifier.  blank=True is kept so that the DB-level constraint
    # allows exactly one legacy/empty email during migrations; all real users
    # must supply a non-empty email when created.
    email = models.EmailField(
        verbose_name="email address",
        unique=True,
        blank=True,
    )

    USERNAME_FIELD = "email"
    # username is still required when creating users programmatically and via
    # createsuperuser — it identifies the user in logs and the admin UI.
    REQUIRED_FIELDS = ["username"]

    is_store_admin = models.BooleanField(default=False)
    is_super_admin = models.BooleanField(default=False)

    class Meta:
        verbose_name = "user"
        verbose_name_plural = "users"


class StoreOwnedModel(models.Model):
    """
    Abstract base class for every store-scoped model (ADR-001 §4).

    Invariants:
    - Every concrete subclass carries a `store` FK with on_delete=PROTECT
      (protects against accidental store deletion cascading to live data).
    - `objects` is a StoreScopedManager whose get_queryset() raises on unscoped
      access; callers must use .for_store(store) or .cross_store_unsafe().
    - Composite DB indexes on hot tables should lead with store_id
      (e.g. (store_id, created_at)) — responsibility of each concrete model.

    Adding a store-scoped model without inheriting this base must fail code review
    and the model-introspection unit test (see core/tests/test_store_owned_models.py).
    """

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.PROTECT,
        # db_index is True by default; composite leading indexes added per model.
    )

    objects = StoreScopedManager()

    class Meta:
        abstract = True

    def validate_unique(self, exclude=None):
        """
        Wraps Model.validate_unique() with the ADR-031 Addendum 3 validation
        window so Django's internal `_perform_unique_checks` (base.py:1538)
        can call `_default_manager.filter(...).exists()` without raising
        IsolationError.

        The window is open ONLY for the duration of the super() call — it
        does not cover any code before or after it, including subclass
        overrides that run their own logic around a super().validate_unique()
        call (they get the window only for their own super() call, same as
        here; anything a subclass does directly with `Model.objects` outside
        that inner super() call stays loud, exactly like normal application
        code). try/finally guarantees the window closes even if
        ValidationError is raised.
        """
        token = _MODEL_VALIDATION_WINDOW.set(True)
        try:
            return super().validate_unique(exclude=exclude)
        finally:
            _MODEL_VALIDATION_WINDOW.reset(token)

    def validate_constraints(self, exclude=None):
        """
        Same wrapper as validate_unique(), for UniqueConstraint.validate()
        (constraints.py:573), which has no queryset-injection hook in its
        signature and so must go through the same contextvar window.
        """
        token = _MODEL_VALIDATION_WINDOW.set(True)
        try:
            return super().validate_constraints(exclude=exclude)
        finally:
            _MODEL_VALIDATION_WINDOW.reset(token)
