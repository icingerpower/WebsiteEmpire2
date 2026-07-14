"""
Anthropic Messages API wrapper for the sales-assistant chat (ADR-024 Decision 1,
Decision 5).

Lazy/guarded import: `anthropic` (added to requirements.txt, approved 2026-07-10
per ADR-024) is imported at module load time inside a try/except, never assumed
present — this module (and everything importing it) must load cleanly in any
environment where the package is not installed, per ADR-024's test strategy
("all Anthropic interactions mocked at the SDK boundary... zero network in
CI"). `get_client()` is the ONE call site that raises if the package is truly
absent; every test patches `chat.anthropic_client.get_client` (or passes a fake
client object matching the SDK's `.messages.stream(...)` interface directly to
`stream_turn`) so production code never has to special-case "package missing"
at the call site.

stream_turn() is the manual tool-use loop (ADR-024 Decision 5): capped at
settings.CHAT_MAX_TOOL_ROUNDTRIPS round-trips, after which one final call is
made with no `tools` parameter so the model is forced to answer with whatever
it already has (AC-CHAT-15). It is a generator so chat/views.py can forward
text deltas as SSE `data:` frames while the turn is still in progress, or drain
it fully for the `?stream=0` buffered fallback (ADR-024 Decision 4 — "same code
path, buffered").
"""

import json
import logging

from django.conf import settings
from django.utils.translation import gettext

logger = logging.getLogger(__name__)

try:
    import anthropic
except ImportError:  # pragma: no cover — exercised by test_anthropic_client_import.py
    anthropic = None


def _upstream_error_message() -> str:
    """
    Generic, translated message shown to the shopper for ANY Anthropic SDK/network
    failure (timeout, 429/5xx, connection reset, ...) — security audit M1: the raw
    exception string must never reach the client (it can carry request ids,
    upstream URLs, library internals). Full detail always goes to
    logger.exception() at the call site; only this generic sentence is
    user-visible. Called at yield time (not import time) so `gettext()` picks
    up the language `stores.middleware.LocaleMiddleware` activated for this
    request (ADR-021).
    """
    return gettext("The assistant is temporarily unavailable. Please try again in a moment.")


def get_client():
    """
    Return a configured `anthropic.Anthropic()` client.

    Raises RuntimeError if the `anthropic` package is not installed. Tests never
    reach this — they patch it out or pass a fake client directly to stream_turn().

    Security audit H1: an explicit `timeout=` is REQUIRED here. Without it the
    SDK applies its own ~600s default, and each `/chat/message/` reply holds a
    WSGI worker for the whole `client.messages.stream()` call (chat/views.py) —
    a single slow/hung upstream connection can pin a worker for minutes. A bare
    float applies uniformly to connect/read/write/pool (per the SDK's `timeout`
    contract), which is what we want here: fail fast and let stream_turn's
    except-block below turn it into a graceful, user-visible error instead of a
    stuck request.
    """
    if anthropic is None:
        raise RuntimeError(
            "The 'anthropic' package is not installed. Add it via requirements.txt "
            "(already present, approved 2026-07-10 per ADR-024) and `pip install -r "
            "requirements.txt`."
        )
    return anthropic.Anthropic(timeout=settings.CHAT_API_TIMEOUT_SECONDS)


def resolve_model_id(store_chat_settings) -> str:
    """
    Model id resolution order (ADR-024 Decision 5, AC-CHAT-16):
    StoreChatSettings.model_id_override -> settings.CHAT_MODEL_ID.
    """
    override = getattr(store_chat_settings, "model_id_override", "") if store_chat_settings else ""
    return override or settings.CHAT_MODEL_ID


def _extract_final_text(response) -> str:
    """Concatenate every text block on a Messages API response."""
    parts = []
    for block in getattr(response, "content", None) or []:
        if getattr(block, "type", None) == "text":
            parts.append(block.text)
    return "".join(parts)


def stream_turn(
    *,
    client,
    model_id: str,
    system_blocks: list,
    tool_specs: list,
    conversation: list,
    max_tool_roundtrips: int | None = None,
    reply_max_tokens: int | None = None,
):
    """
    Execute one user turn against the Messages API, yielding event dicts:

      {"type": "text", "text": str}
          A text delta — forward verbatim as an SSE `data:` frame.
      {"type": "status", "tools": [tool_name, ...]}
          The model is about to call one or more tools this round — forward as
          an SSE `event: status` frame (ADR-024 Decision 4).
      {"type": "final", "text": str, "prompt_tokens": int, "completion_tokens": int,
       "tool_trace": [...]}
          The turn is complete. Always the last event on success.
      {"type": "error", "message": str}
          A 429/500/refusal/connection failure occurred (AC-CHAT-12). Always the
          last event when yielded — no "final" event follows.

    `tool_specs` is chat/tools.py's build_tool_specs() output:
    [{"definition": <tool schema dict>, "handler": callable(input: dict) -> dict}, ...]

    `conversation` is the rebuilt message history (ADR-024 Decision 4) — this
    function appends to a local copy and never mutates the caller's list.
    """
    max_tool_roundtrips = (
        max_tool_roundtrips
        if max_tool_roundtrips is not None
        else settings.CHAT_MAX_TOOL_ROUNDTRIPS
    )
    reply_max_tokens = (
        reply_max_tokens if reply_max_tokens is not None else settings.CHAT_REPLY_MAX_TOKENS
    )

    tool_defs = [spec["definition"] for spec in tool_specs]
    handlers = {spec["definition"]["name"]: spec["handler"] for spec in tool_specs}

    messages = list(conversation)
    tool_trace: list[dict] = []
    total_prompt_tokens = 0
    total_completion_tokens = 0
    final_text_parts: list[str] = []
    response = None

    for round_index in range(max_tool_roundtrips + 1):
        allow_tools = round_index < max_tool_roundtrips
        if not allow_tools:
            # Final forced round: no tools offered, so the model must answer with
            # whatever it has already gathered (ADR-024 Decision 5, AC-CHAT-15).
            messages.append({
                "role": "user",
                "content": (
                    "You have reached the tool-call limit for this turn. Answer "
                    "the visitor now using only the information you already "
                    "gathered from tool results above."
                ),
            })

        request_kwargs = dict(
            model=model_id,
            max_tokens=reply_max_tokens,
            system=system_blocks,
            messages=messages,
        )
        if allow_tools:
            request_kwargs["tools"] = tool_defs

        round_text_parts: list[str] = []
        try:
            with client.messages.stream(**request_kwargs) as stream:
                for text in stream.text_stream:
                    round_text_parts.append(text)
                    final_text_parts.append(text)
                    yield {"type": "text", "text": text}
                response = stream.get_final_message()
        except Exception as exc:  # noqa: BLE001 — any SDK/network failure is user-visible (AC-CHAT-12)
            # Security audit M1: never forward str(exc) to the client (can carry
            # request ids, upstream URLs, library internals) — log the detail,
            # send only the generic translated message.
            logger.exception("Anthropic API call failed during a chat turn.")
            yield {"type": "error", "message": _upstream_error_message()}
            return

        usage = getattr(response, "usage", None)
        total_prompt_tokens += getattr(usage, "input_tokens", 0) or 0
        total_completion_tokens += getattr(usage, "output_tokens", 0) or 0

        stop_reason = getattr(response, "stop_reason", None)

        if stop_reason == "refusal":
            yield {"type": "error", "message": gettext("The assistant declined to answer that.")}
            return

        if stop_reason != "tool_use" or not allow_tools:
            break

        tool_use_blocks = [
            block for block in (response.content or []) if getattr(block, "type", None) == "tool_use"
        ]
        if not tool_use_blocks:
            break

        yield {"type": "status", "tools": [block.name for block in tool_use_blocks]}

        messages.append({"role": "assistant", "content": response.content})
        tool_results = []
        for block in tool_use_blocks:
            handler = handlers.get(block.name)
            tool_input = block.input or {}
            if handler is None:
                result_content = {"error": f"Unknown tool {block.name!r}"}
                is_error = True
            else:
                try:
                    result_content = handler(tool_input)
                    is_error = False
                except Exception:  # noqa: BLE001 — a tool bug must not crash the turn
                    logger.exception("Tool %r raised during a chat turn.", block.name)
                    result_content = {"error": "Tool execution failed."}
                    is_error = True
            tool_trace.append({"tool": block.name, "input": tool_input, "output": result_content})
            result_block = {
                "type": "tool_result",
                "tool_use_id": block.id,
                "content": json.dumps(result_content),
            }
            if is_error:
                result_block["is_error"] = True
            tool_results.append(result_block)
        messages.append({"role": "user", "content": tool_results})

    final_text = "".join(final_text_parts) or _extract_final_text(response)
    yield {
        "type": "final",
        "text": final_text,
        "prompt_tokens": total_prompt_tokens,
        "completion_tokens": total_completion_tokens,
        "tool_trace": tool_trace,
    }
