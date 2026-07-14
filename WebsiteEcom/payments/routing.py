"""
Payment routing engine — full evaluator (ADR-006-R §2, TICKET-020R).

Public API:
  route_payment(order, method_family='card') -> ProcessorAccount | None
      Real routing: increments counters, writes one DecisionLog row, pins the
      processor_account and organization on the order.  Never raises to the caller.

  preview_route(store, country, currency, total_cents, method_family='card') -> bool
      Dry run for AC-134 (hide method when no eligible route exists).
      Identical evaluation path, zero side effects.

Internal pure helpers (unit-testable without DB):
  conditions_match(conditions_json, ctx)  -> bool
  org_over_cap(org)                       -> bool

Internal DB helpers:
  walk_branch(rule, ctx, evaluated, visited, commit, period, depth) -> tuple | None
  resolve_outcome(rule, ctx, evaluated, period, commit)             -> (list, dict | None)
  select_account(org, ctx, commit, period)                          -> ProcessorAccount | None

Design notes:
- The entire routing decision runs inside a single transaction.atomic().
- Counter reads/increments use SELECT FOR UPDATE (AC-131/AC-133).
- route_payment catches all exceptions and degrades to returning None (logs at ERROR).
- preview_route fails open (returns True) on unexpected errors so checkout is not
  blocked by routing infra issues.
- Phase-1 constant: is_test = True for all routing (live-mode lands in Phase 2).
"""

from __future__ import annotations

import logging
from typing import TYPE_CHECKING

from django.db import transaction
from django.db.models import F
from django.utils import timezone

if TYPE_CHECKING:
    from payments.models import ProcessorAccount

logger = logging.getLogger("payments.routing")

# Phase 1: always route to test/sandbox accounts.
# Change to False (or make environment-driven) when live-mode credentials are ready.
_PHASE1_IS_TEST: bool = True

# Maximum fallback-chain depth inside walk_branch (defense-in-depth behind save-time check).
_MAX_DEPTH: int = 20


# ---------------------------------------------------------------------------
# Context builder
# ---------------------------------------------------------------------------


def _build_ctx(order, method_family: str) -> dict:
    """
    Build the routing evaluation context from an Order instance.

    buyer_country: shipping address country (upper), billing fallback, or "".
    currency: order.currency.
    total_cents: int(order.total * 100).
    period: "YYYY-MM" — counter bucket.
    is_test: Phase-1 constant.
    method_family: passed through from the caller.
    """
    shipping = order.shipping_address or {}
    billing = order.billing_address or {}
    raw_country = (
        shipping.get("country") or billing.get("country") or ""
    )
    return {
        "buyer_country": raw_country.upper().strip(),
        "currency": order.currency,
        "total_cents": int(order.total * 100),
        "period": timezone.now().strftime("%Y-%m"),
        "is_test": _PHASE1_IS_TEST,
        "method_family": method_family,
    }


def _build_preview_ctx(country: str, currency: str, total_cents: int, method_family: str) -> dict:
    return {
        "buyer_country": (country or "").upper().strip(),
        "currency": currency,
        "total_cents": total_cents,
        "period": timezone.now().strftime("%Y-%m"),
        "is_test": _PHASE1_IS_TEST,
        "method_family": method_family,
    }


# ---------------------------------------------------------------------------
# conditions_match — pure function, no DB (§2 Step 2)
# ---------------------------------------------------------------------------


def conditions_match(conditions_json: dict, ctx: dict) -> bool:
    """
    Return True if the routing context satisfies the rule's conditions.

    Country logic (§1.8):
    - Effective country set = countries ∪ resolve(areas).
    - Empty conditions OR "ALL" area OR "ROW" area = catch-all → matches any buyer.
    - Non-catch-all rule with empty buyer_country → no match (empty buyer_country
      ONLY matches catch-all rules).

    Currency and amount checks are straightforward bounds.
    """
    if not isinstance(conditions_json, dict):
        return False

    # ---- Country ----
    effective = _effective_country_set(conditions_json)
    if effective is not None:
        # This is a specific-country rule (not catch-all).
        if not ctx["buyer_country"]:
            return False  # empty buyer_country only matches catch-all (§2 Step 2)
        if ctx["buyer_country"] not in effective:
            return False

    # ---- Currency ----
    currencies = conditions_json.get("currencies", [])
    if currencies and ctx["currency"] not in currencies:
        return False

    # ---- Amount bounds ----
    min_cents = conditions_json.get("min_amount_cents")
    max_cents = conditions_json.get("max_amount_cents")
    if min_cents is not None and ctx["total_cents"] < min_cents:
        return False
    if max_cents is not None and ctx["total_cents"] > max_cents:
        return False

    return True


def _effective_country_set(conditions_json: dict) -> frozenset[str] | None:
    """
    Expand conditions_json to an effective country set.

    Returns None for catch-all rules (empty conditions, ALL area, ROW area, or
    no country/area keys at all).  Otherwise returns the union of explicit country
    codes and EU area expansion.

    Mirrors payments.models._expand_countries but is kept here so routing.py
    has no import dependency on the models module at module load time.
    """
    if not conditions_json:
        return None  # empty dict = catch-all

    explicit: set[str] = set(conditions_json.get("countries", []))
    areas: set[str] = set(conditions_json.get("areas", []))

    if "ALL" in areas or "ROW" in areas:
        return None  # catch-all

    if not explicit and not areas:
        return None  # no country/area keys at all = catch-all

    result = set(explicit)
    if "EU" in areas:
        from payments.models import EU_COUNTRIES
        result |= EU_COUNTRIES
    return frozenset(result)


# ---------------------------------------------------------------------------
# org_over_cap — pure function (§2 Step 5)
# ---------------------------------------------------------------------------


def org_over_cap(org) -> bool:
    """
    Return True when the organization has exceeded its monthly volume cap.

    monthly_threshold NULL → never over cap (the default org must always be NULL,
    enforced by DB CheckConstraint 'organization_default_no_cap').
    """
    if org.monthly_threshold is None:
        return False
    return org.current_month_volume >= org.monthly_threshold


# ---------------------------------------------------------------------------
# resolve_outcome — maps a rule to an ordered candidate org list (§2 Step 4)
# ---------------------------------------------------------------------------


def resolve_outcome(
    rule,
    ctx: dict,
    evaluated: list,
    period: str,
    commit: bool,
) -> tuple[list, dict | None]:
    """
    Return (candidate_orgs, split_info) for the given rule.

    candidate_orgs: ordered list of Organization instances to try.
    split_info: dict with pool/period/counter info for percent_split increment,
                or None if this is a direct-org outcome or threshold_switch.

    For outcome_organization: returns ([org], None).
    For outcome_org_pool: applies the pool's active stage-2 rule:
      - percent_split: FOR UPDATE all counter rows, compute deficit ordering.
      - threshold_switch: members ordered by position (cap filtering in walk_branch).

    Stage-2 rule missing or inactive for a pool → returns ([], None) (falls through).
    """
    from payments.models import (
        OrganizationRule,
        OrgPoolMember,
        ProcessorSplitCounter,
    )

    if rule.outcome_organization_id is not None:
        return [rule.outcome_organization], None

    # Pool outcome
    pool = rule.outcome_org_pool
    if pool is None:
        return [], None

    # Find the pool's active stage-2 rule (lowest priority, then pk).
    try:
        s2_rule = (
            OrganizationRule.objects.filter(
                stage="stage2_allocation",
                org_pool=pool,
                is_active=True,
            )
            .order_by("priority_within_stage", "pk")
            .first()
        )
    except Exception:
        return [], None

    if s2_rule is None:
        evaluated.append({"pool": pool.pk, "verdict": "no_stage2_rule"})
        return [], None

    members = list(
        OrgPoolMember.objects.filter(pool=pool)
        .select_related("organization")
        .order_by("position")
    )
    if not members:
        return [], None

    if s2_rule.allocation_type == "threshold_switch":
        # Members already ordered by position; org_over_cap filtering happens in walk_branch.
        return [m.organization for m in members], None

    if s2_rule.allocation_type == "percent_split":
        if commit:
            # Ensure counter rows exist for all members this period.
            for member in members:
                ProcessorSplitCounter.objects.get_or_create(
                    org_pool=pool,
                    organization=member.organization,
                    period=period,
                    defaults={
                        "count": 0,
                        "payment_method": None,
                        "processor_account": None,
                    },
                )
            # Lock all rows for this (pool, period) in one query.
            counters_qs = ProcessorSplitCounter.objects.select_for_update().filter(
                org_pool=pool,
                period=period,
            )
            counter_map: dict = {c.organization_id: c for c in counters_qs}
        else:
            # Preview mode: read existing counters without creating or locking rows.
            existing = ProcessorSplitCounter.objects.filter(
                org_pool=pool,
                period=period,
            )
            counter_map = {c.organization_id: c for c in existing}

        total_count = sum(c.count for c in counter_map.values())

        # Compute deficit = target_percent/100 - count/total (total=0 → deficit = target_percent).
        def deficit(member):
            target = float(member.target_percent)
            c = counter_map.get(member.organization_id)
            cnt = c.count if c is not None else 0
            if total_count == 0:
                return target
            return target / 100 - cnt / total_count

        sorted_members = sorted(members, key=lambda m: (-deficit(m), m.position))
        split_info = {
            "pool": pool,
            "period": period,
        }
        return [m.organization for m in sorted_members], split_info

    # Unknown allocation_type
    return [], None


# ---------------------------------------------------------------------------
# select_account — method-family strategy layer (§2 Step 6)
# ---------------------------------------------------------------------------


def select_account(org, ctx: dict, commit: bool, period: str):
    """
    Select a ProcessorAccount for the given org using the method strategy (§2 Step 6).

    Returns the best eligible ProcessorAccount or None.

    Eligible = is_active AND health_status != RED AND is_test_mode == ctx.is_test
               AND (supported_countries empty OR buyer_country in it).

    backup_chain: sort by (may_be_primary DESC, GREEN-first, option.position,
                           priority_within_family, pk).  Backups fill in only when
                           no primary-eligible candidate exists.
    alternate_evenly: pick lowest counter (tie: option position); increment in-txn
                      when commit=True.
    """
    from payments.models import PaymentMethod, ProcessorOption, ProcessorSplitCounter

    method_family = ctx["method_family"]
    buyer_country = ctx["buyer_country"]
    is_test = ctx["is_test"]

    try:
        method = PaymentMethod.objects.get(method_family=method_family, is_enabled=True)
    except PaymentMethod.DoesNotExist:
        return None

    # Load options for this method that belong to this org, ordered for fallback.
    options = list(
        ProcessorOption.objects.filter(
            payment_method=method,
            processor_account__organization=org,
        )
        .select_related("processor_account")
        .order_by("position", "processor_account__priority_within_family", "processor_account__pk")
    )

    # Filter eligible accounts.
    eligible: list[tuple] = []  # (ProcessorOption, ProcessorAccount)
    for opt in options:
        acct = opt.processor_account
        if not acct.is_active:
            continue
        if acct.health_status == "RED":
            continue
        if acct.is_test_mode != is_test:
            continue
        if acct.supported_countries:
            from core.choices import expand_country_list
            if buyer_country not in expand_country_list(acct.supported_countries):
                continue
        eligible.append((opt, acct))

    if not eligible:
        return None

    if method.strategy == "backup_chain":
        primaries = [(o, a) for o, a in eligible if not a.backup_only]
        pool = primaries if primaries else eligible

        def backup_chain_key(oa):
            o, a = oa
            # GREEN sorts before YELLOW/UNKNOWN (0 vs 1).
            health_order = 0 if a.health_status == "GREEN" else 1
            return (not a.may_be_primary, health_order, o.position, a.priority_within_family, a.pk)

        pool.sort(key=backup_chain_key)
        return pool[0][1]

    if method.strategy == "alternate_evenly":
        if not commit:
            # Preview: return first eligible without touching counters.
            return eligible[0][1]

        eligible_acct_ids = [a.pk for _, a in eligible]

        # Ensure counter rows exist.
        for _, acct in eligible:
            ProcessorSplitCounter.objects.get_or_create(
                payment_method=method,
                processor_account=acct,
                period=period,
                defaults={
                    "count": 0,
                    "org_pool": None,
                    "organization": None,
                },
            )

        # Lock counters.
        counters_qs = ProcessorSplitCounter.objects.select_for_update().filter(
            payment_method=method,
            processor_account_id__in=eligible_acct_ids,
            period=period,
        )
        counter_map = {c.processor_account_id: c.count for c in counters_qs}

        # Pick lowest count, tie-break by option position.
        def evenly_key(oa):
            o, a = oa
            return (counter_map.get(a.pk, 0), o.position)

        eligible.sort(key=evenly_key)
        chosen_acct = eligible[0][1]

        # Increment in-txn.
        ProcessorSplitCounter.objects.filter(
            payment_method=method,
            processor_account=chosen_acct,
            period=period,
        ).update(count=F("count") + 1)

        return chosen_acct

    return None


# ---------------------------------------------------------------------------
# walk_branch — decision-tree traversal (§2 Steps 3–7)
# ---------------------------------------------------------------------------


def walk_branch(
    rule,
    ctx: dict,
    evaluated: list,
    commit: bool,
    period: str,
    visited: frozenset | None = None,
    depth: int = 0,
) -> tuple | None:
    """
    Walk the routing rule branch and return (account, over_cap, reason) or None.

    None means all candidates were ineligible and the fallback chain (if any) was
    also exhausted — caller should fall through to the default org.

    over_cap in the return tuple is always False (only the DecisionLog on route_payment
    has over_cap=True; the walk result itself signals success or failure).

    visited is a frozenset (immutable) so recursion is cycle-safe without mutation.
    """
    if visited is None:
        visited = frozenset()

    if rule.pk in visited or depth > _MAX_DEPTH:
        logger.warning(
            "walk_branch: cycle guard or depth limit hit on rule pk=%s depth=%s",
            rule.pk, depth,
        )
        return None

    visited = visited | {rule.pk}

    # Derive the numeric stage for audit trail entries from the rule's stage field.
    # stage1_geography → 1, stage2_allocation → 2.
    stage_num = 1 if rule.stage == "stage1_geography" else 2

    candidate_orgs, split_info = resolve_outcome(rule, ctx, evaluated, period=period, commit=commit)

    any_candidate = bool(candidate_orgs)
    all_over_cap = True  # assume until we find a non-cap failure

    for org in candidate_orgs:
        if org.status != "active":
            all_over_cap = False
            evaluated.append({
                "rule": rule.pk, "stage": stage_num, "org": org.pk,
                "verdict": "no_eligible_account",
            })
            continue

        if org_over_cap(org):
            evaluated.append({
                "rule": rule.pk, "stage": stage_num, "org": org.pk,
                "verdict": "over_cap",
            })
            continue  # keep all_over_cap = True

        all_over_cap = False
        account = select_account(org, ctx, commit=commit, period=period)

        if account:
            evaluated.append({
                "rule": rule.pk, "stage": stage_num, "org": org.pk, "account": account.pk,
                "verdict": "match",
            })
            # Increment percent_split counter ONLY after successful account selection (§2 step 7b).
            if commit and split_info is not None:
                ProcessorSplitCounter_cls = _get_split_counter_model()
                ProcessorSplitCounter_cls.objects.filter(
                    org_pool=split_info["pool"],
                    organization=org,
                    period=split_info["period"],
                ).update(count=F("count") + 1)

            reason = "rule_match" if depth == 0 else "fallback_chain"
            return account, False, reason

        evaluated.append({
            "rule": rule.pk, "stage": stage_num, "org": org.pk,
            "verdict": "no_eligible_account",
        })

    if not any_candidate:
        all_over_cap = False

    # Try explicit fallback chain (§IX).
    if rule.fallback_rule_id:
        # Load fallback lazily to avoid N+1 when chain is not used.
        try:
            fb = rule.fallback_rule
        except Exception:
            fb = None
        if fb is not None and fb.is_active:
            evaluated.append({"rule": rule.pk, "stage": stage_num, "verdict": "fell_through"})
            return walk_branch(fb, ctx, evaluated, commit, period, visited, depth + 1)

    return None


def _get_split_counter_model():
    from payments.models import ProcessorSplitCounter
    return ProcessorSplitCounter


# ---------------------------------------------------------------------------
# DecisionLog writer
# ---------------------------------------------------------------------------


def _write_decision_log(
    order,
    ctx: dict,
    evaluated: list,
    account,
    matched_rule,
    reason: str,
    over_cap: bool,
    method_family: str,
) -> None:
    """Write exactly one DecisionLog row per route_payment() call."""
    from payments.models import DecisionLog
    chosen_org = account.organization if account else None
    DecisionLog.objects.create(
        order_id=order.pk,
        store_id=order.store_id,
        evaluated_rules_json=evaluated,
        chosen_organization=chosen_org,
        chosen_processor_account=account,
        matched_rule=matched_rule,
        reason=reason,
        over_cap=over_cap,
        buyer_country=ctx["buyer_country"],
        method_family=method_family,
    )


# ---------------------------------------------------------------------------
# route_payment — public API (§2)
# ---------------------------------------------------------------------------


def route_payment(order, method_family: str = "card"):
    """
    Select a ProcessorAccount for the given order.

    Increments counters, writes exactly one DecisionLog row, pins
    order.processor_account and order.organization.

    Never raises to the caller: all exceptions are caught, logged at ERROR,
    and None is returned.  A failure-path DecisionLog row is written (best-effort).
    """
    try:
        with transaction.atomic():
            return _route_payment_internal(order, method_family)
    except Exception as exc:
        logger.error(
            "route_payment failed unexpectedly: order=%s method=%s error=%s",
            getattr(order, "pk", None), method_family, exc,
            exc_info=True,
        )
        # Best-effort failure log outside the failed transaction.
        try:
            _write_failure_log(order, method_family)
        except Exception as log_exc:
            logger.error("route_payment: failed to write failure DecisionLog: %s", log_exc)
        return None


def _route_payment_internal(order, method_family: str):
    """Inner routing implementation — runs inside transaction.atomic()."""
    from payments.models import OrganizationRule
    from stores.models import Organization

    ctx = _build_ctx(order, method_family)
    evaluated: list = []

    account = None
    over_cap = False
    reason = "fallback_default_org"
    matched_rule = None

    # ---- Stage 1: geography rules (AC-130) --------------------------------
    stage1_rules = OrganizationRule.objects.filter(
        stage="stage1_geography",
        is_active=True,
    ).order_by("priority_within_stage", "pk")

    for rule in stage1_rules:
        if conditions_match(rule.conditions_json, ctx):
            matched_rule = rule
            result = walk_branch(
                rule, ctx, evaluated,
                commit=True, period=ctx["period"],
            )
            if result is not None:
                account, over_cap, reason = result
            else:
                # Branch exhausted: determine why (cap vs health).
                over_cap = _all_cap_exhaustion(evaluated)
                reason = "all_orgs_at_cap" if over_cap else "fallback_chain"
            break
        else:
            evaluated.append({"rule": rule.pk, "stage": 1, "verdict": "no_match"})

    # ---- Fallback to default org (AF-C6) -----------------------------------
    if account is None:
        try:
            default_org = Organization.objects.get(is_default=True, status="active")
        except Organization.DoesNotExist:
            default_org = None

        if default_org is not None:
            account = select_account(
                default_org, ctx, commit=True, period=ctx["period"]
            )

        if matched_rule is None and account is not None:
            reason = "fallback_default_org"
        if account is None:
            reason = "routing_failure"

    # ---- Write DecisionLog (every call, no sampling) (§2 Step 9) ----------
    _write_decision_log(order, ctx, evaluated, account, matched_rule, reason, over_cap, method_family)

    # ---- Pin order ---------------------------------------------------------
    if account is not None:
        order.processor_account = account
        order.organization = account.organization
        order.save(update_fields=["processor_account", "organization"])

    return account


def _all_cap_exhaustion(evaluated: list) -> bool:
    """
    Return True only when every candidate org failure was due to monthly cap.

    Used to set reason='all_orgs_at_cap' vs 'fallback_chain' when walk_branch
    returns None.
    """
    candidate_verdicts = [
        e["verdict"] for e in evaluated
        if e.get("verdict") in ("over_cap", "no_eligible_account")
    ]
    if not candidate_verdicts:
        return False
    return all(v == "over_cap" for v in candidate_verdicts)


def _write_failure_log(order, method_family: str) -> None:
    """Write a routing_failure DecisionLog when the routing transaction itself crashed."""
    from payments.models import DecisionLog
    DecisionLog.objects.create(
        order_id=order.pk,
        store_id=order.store_id,
        evaluated_rules_json=[],
        chosen_organization=None,
        chosen_processor_account=None,
        matched_rule=None,
        reason="routing_failure",
        over_cap=False,
        buyer_country="",
        method_family=method_family,
    )


# ---------------------------------------------------------------------------
# preview_route — dry run, no side effects (AC-134)
# ---------------------------------------------------------------------------


def preview_route(
    store,
    country: str,
    currency: str,
    total_cents: int,
    method_family: str = "card",
) -> bool:
    """
    Return True if a valid ProcessorAccount would be found for these parameters.

    Identical evaluation to route_payment but NO counter increments, NO DecisionLog
    write, and NO order pinning.  Used by checkout to hide payment methods when no
    route exists (AC-134) and to reject orders server-side before creation.

    Fails open (returns True) on unexpected errors so a routing infra issue does
    not block checkout entirely.
    """
    try:
        return _preview_route_internal(country, currency, total_cents, method_family)
    except Exception as exc:
        logger.error("preview_route failed: %s", exc, exc_info=True)
        return True  # fail open


def _preview_route_internal(
    country: str,
    currency: str,
    total_cents: int,
    method_family: str,
) -> bool:
    """Inner preview — read-only evaluation, no writes."""
    from payments.models import OrganizationRule
    from stores.models import Organization

    ctx = _build_preview_ctx(country, currency, total_cents, method_family)
    evaluated: list = []  # not used for output but keeps helper signatures identical

    # Stage 1: geography rules
    stage1_rules = OrganizationRule.objects.filter(
        stage="stage1_geography",
        is_active=True,
    ).order_by("priority_within_stage", "pk")

    for rule in stage1_rules:
        if conditions_match(rule.conditions_json, ctx):
            result = walk_branch(
                rule, ctx, evaluated,
                commit=False, period=ctx["period"],
            )
            if result is not None:
                account, _, _ = result
                return account is not None
            break
        # else: keep looking for a matching rule

    # Fallback to default org
    try:
        default_org = Organization.objects.get(is_default=True, status="active")
    except Organization.DoesNotExist:
        return False

    account = select_account(default_org, ctx, commit=False, period=ctx["period"])
    return account is not None
