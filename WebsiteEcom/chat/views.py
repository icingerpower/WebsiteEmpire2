"""
Storefront endpoints for the sales-assistant chat (ADR-024 Decision 4).

  POST /chat/session/   — mint a ChatSession + signed token.
  POST /chat/message/    — post one user message; SSE reply by default,
                           `?stream=0` returns the identical final text as
                           JSON (same underlying code path — AC-CHAT-06).

Both views require the storefront CSRF token (no @csrf_exempt — the normal
CsrfViewMiddleware behavior from webecom/settings/base.py applies unchanged).

Refusal contract (ADR-024 Decision 4/6/7, AC-CHAT-01/07/08/09/10/11): every
pre-flight rejection returns `JsonResponse({"ok": False, "reason": "..."})`
with an explicit machine-readable reason — never a silent/blank response
(design-pattern-ideas.txt §XV-1). Only once every pre-flight check passes does
either an SSE stream or a buffered JSON success response begin.
"""

import json
import logging

from django.conf import settings
from django.db.models import F
from django.http import HttpResponseNotAllowed, JsonResponse, StreamingHttpResponse
from django.utils import timezone

from aijobs.service import check_quota
from chat.anthropic_client import get_client, resolve_model_id, stream_turn
from chat.models import ChatEndedReason, ChatMessage, ChatRole, ChatSession, StoreChatSettings
from chat.quota import daily_message_budget_exceeded, record_usage
from chat.sessions import mint_session_key, sign_session_token, verify_session_token
from chat.system_prompt import build_system_blocks
from chat.tools import build_tool_specs
from pages.antispam import rate_limit_exceeded

logger = logging.getLogger(__name__)

MAX_MESSAGE_LENGTH = 2000
HISTORY_TURNS = 12


def _get_store_chat_settings(store):
    return StoreChatSettings.objects.for_store(store).first()


def _refusal(reason: str, status: int = 403):
    return JsonResponse({"ok": False, "reason": reason}, status=status)


def _gating_refusal(store):
    """
    Shared opt-in-gating checks (ADR-024 Decision 9/10, AC-CHAT-01): kill switch,
    then is_enabled + terms accepted — each independently able to cause a
    refusal. Returns a refusal JsonResponse, or None when the store may proceed.
    """
    if settings.CHAT_KILL_SWITCH:
        return _refusal("kill_switch")
    chat_settings = _get_store_chat_settings(store)
    if chat_settings is None or not chat_settings.is_opted_in:
        return _refusal("not_opted_in")
    return None


def _parse_json_body(request) -> dict:
    try:
        return json.loads(request.body.decode("utf-8") or "{}")
    except (ValueError, UnicodeDecodeError):
        return {}


def chat_session_view(request):
    """POST /chat/session/ — mint a new ChatSession + signed token."""
    if request.method != "POST":
        return HttpResponseNotAllowed(["POST"])

    store = getattr(request, "store", None)
    if store is None:
        return JsonResponse({"ok": False, "reason": "no_store"}, status=400)

    refusal = _gating_refusal(store)
    if refusal is not None:
        return refusal

    if rate_limit_exceeded(request, "chat_session", limit=5, window_seconds=3600):
        return _refusal("rate_limited", status=429)

    locale = getattr(request, "locale", None)
    lang_code = getattr(getattr(locale, "language", None), "lang_code", "") or store.primary_language

    session = ChatSession.objects.create(
        store=store,
        session_key=mint_session_key(),
        locale=lang_code,
    )
    token = sign_session_token(session.session_key)
    return JsonResponse({"ok": True, "session_token": token})


def chat_message_view(request):
    """
    POST /chat/message/ — post one user message, get back an assistant reply.
    Default: SSE. `?stream=0`: buffered JSON with the identical final text.
    """
    if request.method != "POST":
        return HttpResponseNotAllowed(["POST"])

    store = getattr(request, "store", None)
    if store is None:
        return JsonResponse({"ok": False, "reason": "no_store"}, status=400)

    refusal = _gating_refusal(store)
    if refusal is not None:
        return refusal

    body = _parse_json_body(request)
    # History is ALWAYS rebuilt server-side below (AC-CHAT-10) — any
    # "history"-shaped key the client sends is never read.
    session_token = body.get("session_token", "")
    message_text = (body.get("message") or "").strip()

    session_key = verify_session_token(session_token)
    if session_key is None:
        return _refusal("invalid_session", status=400)

    try:
        session = ChatSession.objects.for_store(store).get(session_key=session_key)
    except ChatSession.DoesNotExist:
        return _refusal("session_not_found", status=400)

    if rate_limit_exceeded(request, "chat_message", limit=20, window_seconds=3600):
        return _refusal("rate_limited", status=429)

    chat_settings = _get_store_chat_settings(store)
    daily_budget = chat_settings.daily_message_budget if chat_settings else 500
    if daily_message_budget_exceeded(store, daily_budget):
        return _refusal("store_daily_budget_exceeded", status=429)

    if session.ended_reason != ChatEndedReason.ACTIVE:
        return _refusal(session.ended_reason, status=403)

    now = timezone.now()
    ttl_seconds = settings.CHAT_SESSION_TTL_SECONDS
    if (now - session.last_activity_at).total_seconds() > ttl_seconds:
        session.ended_reason = ChatEndedReason.EXPIRED
        session.save(update_fields=["ended_reason"])
        return _refusal(ChatEndedReason.EXPIRED, status=403)

    if session.message_count >= settings.CHAT_SESSION_MESSAGE_CAP:
        session.ended_reason = ChatEndedReason.MESSAGE_CAP
        session.save(update_fields=["ended_reason"])
        return _refusal(ChatEndedReason.MESSAGE_CAP, status=403)

    if session.total_tokens >= settings.CHAT_SESSION_TOKEN_CAP:
        session.ended_reason = ChatEndedReason.TOKEN_CAP
        session.save(update_fields=["ended_reason"])
        return _refusal(ChatEndedReason.TOKEN_CAP, status=403)

    if not message_text:
        return _refusal("empty_message", status=400)
    if len(message_text) > MAX_MESSAGE_LENGTH:
        return _refusal("message_too_long", status=400)

    if not check_quota(store):
        # AC-CHAT-11: refusal BEFORE any Anthropic call — nothing below this
        # line has touched chat.anthropic_client yet.
        return _refusal("quota_exhausted", status=429)

    # Accept the message: persist it and consume one message-count slot BEFORE
    # calling the model, so a subsequent Anthropic error still counts this turn
    # as used (the session remains usable either way — AC-CHAT-12).
    ChatMessage.objects.create(store=store, session=session, role=ChatRole.USER, content=message_text)
    ChatSession.objects.for_store(store).filter(pk=session.pk).update(
        message_count=F("message_count") + 1,
        last_activity_at=now,
    )
    session.refresh_from_db(fields=["message_count", "last_activity_at"])

    history = list(
        ChatMessage.objects.for_store(store)
        .filter(session=session)
        .order_by("-created_at")[:HISTORY_TURNS]
    )
    history.reverse()
    conversation = [{"role": m.role, "content": m.content} for m in history]

    lang_code = session.locale or store.primary_language
    system_blocks = build_system_blocks(locale_lang_code=lang_code)
    tool_specs = build_tool_specs(store, lang_code, store_chat_settings=chat_settings)
    model_id = resolve_model_id(chat_settings)

    stream_flag = request.GET.get("stream", "1") != "0"
    turn_args = (store, session, model_id, system_blocks, tool_specs, conversation)
    if stream_flag:
        return _stream_sse_response(*turn_args)
    return _buffered_json_response(*turn_args)


def _run_and_persist(store, session, model_id, system_blocks, tool_specs, conversation):
    """
    Drive chat.anthropic_client.stream_turn(); on the terminal 'final' event,
    persist the assistant ChatMessage + record usage (ADR-024 Decision 6)
    before re-yielding a 'final' event carrying the new message id. On
    'error', nothing assistant-side is persisted — the session stays usable
    (AC-CHAT-12); the user's message and message_count increment already
    stand from chat_message_view.
    """
    client = get_client()
    for event in stream_turn(
        client=client,
        model_id=model_id,
        system_blocks=system_blocks,
        tool_specs=tool_specs,
        conversation=conversation,
    ):
        if event["type"] == "final":
            message = ChatMessage.objects.create(
                store=store,
                session=session,
                role=ChatRole.ASSISTANT,
                content=event["text"],
                tool_trace_json=event["tool_trace"],
            )
            record_usage(
                store=store,
                session=session,
                model_id=model_id,
                prompt_tokens=event["prompt_tokens"],
                completion_tokens=event["completion_tokens"],
            )
            yield {"type": "final", "text": event["text"], "message_id": message.pk}
        else:
            yield event


def _sse_frame(event_type: str | None, data: dict) -> str:
    if event_type:
        return f"event: {event_type}\ndata: {json.dumps(data)}\n\n"
    return f"data: {json.dumps(data)}\n\n"


def _stream_sse_response(store, session, model_id, system_blocks, tool_specs, conversation):
    def generate():
        try:
            for event in _run_and_persist(store, session, model_id, system_blocks, tool_specs, conversation):
                if event["type"] == "text":
                    yield _sse_frame(None, {"text": event["text"]})
                elif event["type"] == "status":
                    yield _sse_frame("status", {"tools": event["tools"]})
                elif event["type"] == "error":
                    logger.error("Chat turn error (SSE): %s", event["message"])
                    yield _sse_frame("error", {"message": event["message"]})
                elif event["type"] == "final":
                    yield _sse_frame("done", {"message_id": event["message_id"]})
        except Exception:  # noqa: BLE001 — never let a bug produce a silent hung stream
            logger.exception("Unhandled error while streaming a chat turn.")
            yield _sse_frame("error", {"message": "unexpected_error"})

    response = StreamingHttpResponse(generate(), content_type="text/event-stream")
    response["Cache-Control"] = "no-cache"
    # Disable proxy buffering (nginx) so SSE frames flush immediately.
    response["X-Accel-Buffering"] = "no"
    return response


def _buffered_json_response(store, session, model_id, system_blocks, tool_specs, conversation):
    final_text = ""
    message_id = None
    error_message = None
    for event in _run_and_persist(store, session, model_id, system_blocks, tool_specs, conversation):
        if event["type"] == "final":
            final_text = event["text"]
            message_id = event["message_id"]
        elif event["type"] == "error":
            error_message = event["message"]

    if message_id is None:
        logger.error("Chat turn failed (buffered mode): %s", error_message)
        return JsonResponse({"ok": False, "error": error_message or "unexpected_error"}, status=502)

    return JsonResponse({"ok": True, "message": final_text, "message_id": message_id})
