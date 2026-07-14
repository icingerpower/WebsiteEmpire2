"""
StoreScopedManager — the central guard against cross-tenant data bleed (ADR-001 §4).

Design intent (§XV-1: invisible failures are the enemy):
  - The unscoped query IS the loud failure: calling .all(), .filter(), etc. directly on
    a StoreOwnedModel.objects queryset raises IsolationError unless the caller has
    explicitly opted into a scoped or unsafe path.
  - The ONLY two public entry points are:
      Model.objects.for_store(store)          — returns queryset filtered to that store.
      Model.objects.cross_store_unsafe()      — deliberately ugly; used ONLY in
                                                super-admin reports and data migrations.
  - Any code that calls .all() or .filter() on the base manager without going through
    one of those two paths will blow up at test/review time, never at 3 a.m. in prod.
"""

import contextvars

from django.db import models

# ADR-031 Addendum 3 (2026-07-11, "the validation ring"): a narrow window that
# unlocks ONLY _RaisingQuerySet.exists(), set for the duration of
# StoreOwnedModel.validate_unique()/validate_constraints()'s own super() call
# (core/models.py). Django's internal validation machinery
# (Model._perform_unique_checks / _perform_date_checks in base.py, and
# UniqueConstraint.validate in constraints.py) all go through
# `model_class._default_manager.filter(**lookup_kwargs).exists()` — the raising
# StoreScopedManager for every StoreOwnedModel — so full_clean()/
# validate_unique() on ANY StoreOwnedModel with a unique_together/unique=True/
# UniqueConstraint has raised IsolationError instead of validating, ever since
# TICKET-051 closed exists()'s silent-bypass hole. This contextvar is the
# sanctioned, narrow escape hatch: every constraint on every StoreOwnedModel
# today names/pins `store` (see ADR-031 Addendum 3's constraint enumeration),
# so unlocking exists() here preserves stock Django semantics rather than
# widening what a constraint matches.
_MODEL_VALIDATION_WINDOW = contextvars.ContextVar("store_model_validation", default=False)


class _RaisingQuerySet(models.QuerySet):
    """
    QuerySet subclass whose iteration and evaluation paths all raise.
    Returned by the base get_queryset() so accidental unscoped usage is caught early.

    ADR-031 addendum (2026-07-11, "AGGREGATE-DOESNT-RAISE and its full
    family"): __iter__/__len__/__bool__/_fetch_all only guard paths that go
    through Python-level result fetching. Several QuerySet terminal methods
    execute SQL directly and never call any of those four — silently
    bypassing the isolation guard entirely. Each is overridden below to
    raise unconditionally, closing that hole:

      - aggregate()  -> query.get_aggregation() directly (query.py L594ish)
      - count()      -> query.get_count() directly (bypassed when
                        _result_cache is None, which it always is here)
      - exists()     -> query.has_results() directly
      - iterator()   -> self._iterator() directly (a distinct generator,
                        not __iter__)
      - aiterator()  -> builds self._iterable_class directly; does NOT
                        delegate to iterator(), so it needs its own override
                        (Django 6.0.4 query.py ~L535)
      - update()     -> compiler.execute_sql() directly — a SILENT UNSCOPED
                        CROSS-STORE WRITE, the worst member of this family
      - delete()     -> Collector.delete() directly; the "fast delete" path
                        (Collector.can_fast_delete()) skips fetching objects
                        entirely, so it never touches _fetch_all() either
      - explain()    -> query.explain() directly

    NOT overridden (covered transitively, per the addendum's Risks section —
    pinned by tests, not merely assumed):
      - contains(obj)   -> falls through to self.filter(pk=obj.pk).exists();
                           filter() clones preserve this class, so the raise
                           happens via the exists() override above.
      - bulk_update()   -> falls through to
                           self.using(db).filter(pk__in=pks).update(...) per
                           batch; same cloning argument routes it through the
                           update() override above.
      - get()/first()/last()/in_bulk()/earliest()/latest()/values()/
        values_list() evaluation, and every a<method>() async wrapper that
        merely does `await sync_to_async(self.<sync method>)(...)` — already
        raise today (status-quo pins), unaffected by this change.

    The one sanctioned escape hatch is StoreScopedManager.none(), which
    returns a real (non-raising) empty QuerySet — see its docstring.
    """

    _UNSCOPED_MESSAGE = (
        "Unscoped query on a StoreOwnedModel is forbidden (ADR-001 §4). "
        "Use .for_store(store) or .cross_store_unsafe() explicitly."
    )

    def __iter__(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def __len__(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def __bool__(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def _fetch_all(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def aggregate(self, *args, **kwargs):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def count(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def exists(self):
        # ADR-031 Addendum 3: Django's own validation lookups
        # (base.py:_perform_unique_checks/_perform_date_checks,
        # constraints.py:UniqueConstraint.validate) filter by exactly the
        # constraint's fields before calling exists() — every current
        # constraint includes/pins store, and a future store-independent
        # constraint SHOULD check cross-store, so unlocking exists() here is
        # stock Django semantics, not a relaxation of what gets matched. Only
        # unlocked for the duration of StoreOwnedModel.validate_unique()/
        # validate_constraints()'s super() call (core/models.py) — every
        # other terminal method (update/delete/count/aggregate/iteration)
        # keeps raising even while this window is open.
        if _MODEL_VALIDATION_WINDOW.get():
            return super().exists()
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def iterator(self, chunk_size=None):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def aiterator(self, chunk_size=2000):
        # Deliberately a plain (non-async, non-generator) method: Django's
        # own aiterator() is an async generator function (uses `yield`
        # internally), built independently of iterator() — it must be
        # closed here separately (see class docstring). Raising eagerly and
        # synchronously, before any `async for` begins, is simpler than
        # reproducing the async-generator shape and is equally effective:
        # `async for x in qs.aiterator():` evaluates the `qs.aiterator()`
        # call first, so the IsolationError still propagates immediately.
        raise IsolationError(self._UNSCOPED_MESSAGE)

    def update(self, **kwargs):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    update.alters_data = True

    def delete(self):
        raise IsolationError(self._UNSCOPED_MESSAGE)

    delete.alters_data = True
    delete.queryset_only = True

    def explain(self, *, format=None, **options):
        raise IsolationError(self._UNSCOPED_MESSAGE)


class IsolationError(Exception):
    """Raised when a store-scoped model is queried without an explicit store scope."""


class StoreScopedManager(models.Manager):
    """
    Manager for all StoreOwnedModel subclasses.
    The base get_queryset() returns a _RaisingQuerySet; callers must use
    for_store() or cross_store_unsafe() to obtain a real queryset.
    """

    def get_queryset(self):
        """
        Returns a _RaisingQuerySet for direct/unscoped access — any iteration,
        len, bool or fetch raises IsolationError so unscoped queries are caught
        at development/test time rather than silently returning wrong data.

        ADR-031 exception: when `self` is a relation-bound M2M manager
        (Django's `ManyRelatedManager` — built by subclassing the *target*
        model's `_default_manager.__class__`, per related_descriptors.py, so
        this method runs as `self` for M2M access too), a real queryset is
        returned instead. Detection: `ManyRelatedManager.__init__` sets BOTH
        `self.instance` and `self.through`; the reverse-FK `RelatedManager`
        sets only `self.instance` (no `through`) and must keep raising —
        status quo pinned by
        core/tests/test_m2m_relation_bound_isolation.py::ReverseFkManagerStillRaisesTest.

        Why this is safe: `ManyRelatedManager.get_queryset()` immediately
        pipes this queryset through `_apply_rel_filters()`, which pins every
        row to the through-table rows of `self.instance` — one already-scoped
        object. Cross-store through rows are themselves prevented at write
        time by the core/m2m_guard.py `m2m_changed` guard (ADR-031), so every
        through row already links two same-store objects: a relation-bound
        M2M read can never surface another store's data. Direct/unscoped
        access (`Model.objects.all()`) and reverse-FK related managers keep
        raising exactly as before.
        """
        if getattr(self, "through", None) is not None and getattr(self, "instance", None) is not None:
            return models.QuerySet(self.model, using=self._db, hints=self._hints)
        return _RaisingQuerySet(self.model, using=self._db)

    def for_store(self, store):
        """
        Returns a real queryset filtered to the given store.
        This is the standard entry point for all storefront and store-admin views.
        """
        return models.QuerySet(self.model, using=self._db).filter(store=store)

    def none(self):
        """
        Returns a real, non-raising empty queryset (ADR-031 addendum,
        "sanctioned safe entry point").

        Without this override, `Model.objects.none()` resolves via Python's
        normal method lookup to the queryset-method proxy that
        `models.Manager` auto-generates from `QuerySet.none()`, which calls
        `self.get_queryset().none()` — a `.none()` *clone* of
        `_RaisingQuerySet`. `QuerySet.none()` only calls `query.set_empty()`;
        it does not change `self.__class__`, so the clone is still a
        `_RaisingQuerySet` whose __iter__/__len__/__bool__/_fetch_all
        unconditionally raise regardless of the query being empty — a booby
        trap already planted in several admin `get_queryset(request)`
        no-store fallbacks (e.g. `return Model.objects.none()` when
        `request.store` is None), which would 500 the instant Django's
        ChangeList tries to render that "empty" queryset.

        Defining `none()` directly on this manager overrides the
        auto-generated proxy (normal Python subclass method resolution —
        `StoreScopedManager` methods win over inherited `models.Manager`
        ones), so every existing `Model.objects.none()) call site across the
        codebase is fixed by this one change, with no call-site edits
        needed.
        """
        return models.QuerySet(self.model, using=self._db).none()

    def cross_store_unsafe(self):
        """
        Returns an unfiltered queryset spanning ALL stores.
        Deliberately ugly name — must appear in code review for super-admin reports
        and data migrations only.  Every usage is auditable via grep.
        """
        return models.QuerySet(self.model, using=self._db).all()
