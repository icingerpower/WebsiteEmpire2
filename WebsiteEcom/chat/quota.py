"""
Cost accounting + session/store caps for the sales-assistant chat (ADR-024
Decision 6, Decision 7).

Cost accounting deliberately does NOT create AiJob/AiJobRun rows — chat turns
are not jobs (ADR-024 Decision 6, AC-CHAT-11). Usage is recorded straight into
the existing AiJobMetric (job_type='sales_chat') and StoreAiQuota rows using the
same atomic F()-expression pattern as aijobs.service.record_cost, without
requiring an AiJobRun instance.

Store daily message budget (ADR-024 Decision 7): a cache-based, IP-independent
counter keyed on store only — same pattern as pages.antispam.
contact_notification_budget_exceeded, but NOT implemented by calling into
pages/antispam.py, because that function hardcodes its own cap constant and
cache-key namespace for the contact-form use case. This module reimplements the
same cache.add()+cache.incr() primitive (design-pattern-ideas.txt §XV-4 spirit:
one *pattern*, reused per call site) under its own key namespace, parameterized
by StoreChatSettings.daily_message_budget (a per-store *configurable* cap,
unlike the contact form's fixed constant) — see chat/tests/test_quota.py.
"""

from decimal import Decimal

from django.core.cache import cache
from django.db import transaction
from django.db.models import F
from django.utils import timezone

from aijobs.models import AiJobMetric, StoreAiQuota

JOB_TYPE_SALES_CHAT = "sales_chat"

_DAILY_BUDGET_WINDOW_SECONDS = 86400

# Anthropic list pricing, USD per token (ADR-024 Decision 5's cost model — Haiku
# 4.5 is the default). Unknown model ids fall back to the Haiku 4.5 rate as a
# conservative estimate rather than raising — cost accounting must never block
# a reply (ASSUMPTION: exact per-model pricing table is a release-checklist
# item to keep in sync with Anthropic's published pricing).
_PRICE_PER_TOKEN_USD = {
    "claude-haiku-4-5": {"input": Decimal("1.00") / Decimal("1000000"), "output": Decimal("5.00") / Decimal("1000000")},
}
_DEFAULT_PRICE = _PRICE_PER_TOKEN_USD["claude-haiku-4-5"]


def estimate_cost_usd(model_id: str, prompt_tokens: int, completion_tokens: int) -> Decimal:
    price = _PRICE_PER_TOKEN_USD.get(model_id, _DEFAULT_PRICE)
    return (Decimal(prompt_tokens) * price["input"]) + (Decimal(completion_tokens) * price["output"])


def daily_message_budget_exceeded(store, daily_budget: int) -> bool:
    """
    Return True once `daily_budget` chat messages have already been counted for
    `store` today, independent of source IP (ADR-024 Decision 7).

    Call exactly once per accepted message attempt, before the Anthropic call.
    """
    store_key = getattr(store, "pk", "no-store")
    key = f"chat:daily_budget:{store_key}"

    cache.add(key, 0, _DAILY_BUDGET_WINDOW_SECONDS)
    try:
        count = cache.incr(key)
    except ValueError:
        # Key expired between add() and incr() under a race — treat as first hit.
        cache.add(key, 1, _DAILY_BUDGET_WINDOW_SECONDS)
        count = 1

    return count > daily_budget


@transaction.atomic
def record_usage(*, store, session, model_id: str, prompt_tokens: int, completion_tokens: int) -> Decimal:
    """
    Record one Anthropic call's usage (ADR-024 Decision 6, AC-CHAT-11):

    - Adds to ChatSession.prompt_tokens/completion_tokens/cost_usd (F() atomic).
    - Adds to StoreAiQuota.current_month_spent_usd (F() atomic; no-op if the
      store has no quota row — mirrors aijobs.service.record_cost).
    - Upserts today's AiJobMetric row for job_type='sales_chat' (F() atomic).

    Returns the cost_usd for THIS call (not the cumulative session total).
    """
    cost_usd = estimate_cost_usd(model_id, prompt_tokens, completion_tokens)

    from chat.models import ChatSession

    ChatSession.objects.for_store(store).filter(pk=session.pk).update(
        prompt_tokens=F("prompt_tokens") + prompt_tokens,
        completion_tokens=F("completion_tokens") + completion_tokens,
        cost_usd=F("cost_usd") + cost_usd,
        last_activity_at=timezone.now(),
    )

    today = timezone.now().date()
    current_month = today.replace(day=1)
    StoreAiQuota.objects.for_store(store).filter(current_month=current_month).update(
        current_month_spent_usd=F("current_month_spent_usd") + cost_usd,
    )

    metric, _created = AiJobMetric.objects.get_or_create(
        period_date=today,
        store_id=store.pk,
        job_type=JOB_TYPE_SALES_CHAT,
        model_id=model_id,
        defaults={"run_count": 0, "prompt_tokens": 0, "completion_tokens": 0, "total_cost_usd": 0},
    )
    AiJobMetric.objects.filter(pk=metric.pk).update(
        run_count=F("run_count") + 1,
        prompt_tokens=F("prompt_tokens") + prompt_tokens,
        completion_tokens=F("completion_tokens") + completion_tokens,
        total_cost_usd=F("total_cost_usd") + cost_usd,
    )

    return cost_usd
