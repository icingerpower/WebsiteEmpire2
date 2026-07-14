"""
Chat app models — AI sales-assistant chat (ADR-024, TICKET-038).

Entities:
  ChatSession        — one anonymous chat conversation (store-scoped).
  ChatMessage         — one turn (user or assistant) within a session (store-scoped).
  StoreChatSettings   — per-store opt-in + GDPR acceptance + budget config (store-scoped).

Design rules (ADR-024):
  - All three models extend StoreOwnedModel (ADR-001 §4) — every query MUST go
    through .for_store(store).
  - ChatSession.ended_reason is an explicit enum, never inferred from absence of
    activity (design-pattern-ideas.txt §XV-3): active/message_cap/token_cap/
    expired/killed.
  - Chat is anonymous in v1 — no customer-account linkage (ADR-024 Decision 10).
  - StoreChatSettings.is_enabled defaults to False (GDPR opt-in OFF by default,
    DECIDED-human 2026-07-10). Disabling preserves subprocessor_terms_accepted_at
    (audit trail) — only is_enabled flips, the acceptance timestamp is never cleared.
  - StoreChatSettings also carries the "designated pages" for get_store_policy()
    (chat/tools.py). ADR-024 Decision 3 says the policy tool resolves text "from
    the store's policy-kind and designated pages" but does not specify a schema
    for that designation. ASSUMPTION (flagged for Architect/Safety review): rather
    than adding a policy-kind field to pages.StaticPage (out of this app's
    ownership and a cross-app schema change), the designation lives here as four
    nullable FKs onto pages.StaticPage. This keeps the mapping entirely inside
    the chat app's own migrations.
"""

from django.conf import settings
from django.db import models

from core.models import StoreOwnedModel


class ChatEndedReason(models.TextChoices):
    """
    Explicit session lifecycle enum (ADR-024 Decision 2, design-pattern-ideas §XV-3).

    Never inferred from absence of a row or from a timestamp comparison alone —
    every transition away from ACTIVE is an explicit .save() at the point a request
    is refused because of that specific condition.
    """

    ACTIVE = "active", "Active"
    MESSAGE_CAP = "message_cap", "Message cap reached"
    TOKEN_CAP = "token_cap", "Token budget reached"
    EXPIRED = "expired", "Expired"
    KILLED = "killed", "Killed (kill switch)"


class ChatRole(models.TextChoices):
    USER = "user", "User"
    ASSISTANT = "assistant", "Assistant"


class ChatSession(StoreOwnedModel):
    """
    One anonymous chat conversation (ADR-024 Decision 2).

    session_key is the server-minted opaque identifier. The value handed to the
    browser is a *signed* token wrapping session_key (chat/sessions.py) — the
    client never sees or controls session_key directly, and a tampered token
    fails signature verification before any DB lookup happens.

    Session caps (ADR-024 Decision 6): 30 messages/session, 150K cumulative
    tokens/session — enforced by chat/views.py pre-flight checks, which stamp
    ended_reason at the point a request is refused because a cap was hit.
    """

    session_key = models.CharField(
        max_length=64,
        unique=True,
        db_index=True,
        help_text="Server-minted opaque session identifier (secrets.token_urlsafe).",
    )
    locale = models.CharField(
        max_length=10,
        default="en",
        help_text="ISO 639-1 lang_code the session was minted under (ADR-008).",
    )
    created_at = models.DateTimeField(auto_now_add=True)
    last_activity_at = models.DateTimeField(auto_now_add=True)
    message_count = models.PositiveIntegerField(
        default=0,
        help_text="Number of accepted user messages this session (cap: 30).",
    )
    prompt_tokens = models.PositiveIntegerField(default=0)
    completion_tokens = models.PositiveIntegerField(default=0)
    cost_usd = models.DecimalField(max_digits=10, decimal_places=6, default=0)
    ended_reason = models.CharField(
        max_length=20,
        choices=ChatEndedReason.choices,
        default=ChatEndedReason.ACTIVE,
    )

    class Meta:
        verbose_name = "chat session"
        verbose_name_plural = "chat sessions"
        indexes = [
            models.Index(fields=["store", "created_at"]),
            models.Index(fields=["store", "ended_reason"]),
        ]

    def __str__(self):
        return f"ChatSession {self.pk} [{self.session_key[:8]}...] {self.ended_reason}"

    @property
    def total_tokens(self) -> int:
        return self.prompt_tokens + self.completion_tokens


class ChatMessage(StoreOwnedModel):
    """
    One turn (user or assistant) within a ChatSession (ADR-024 Decision 2).

    tool_trace_json audits every tool call + result made while producing an
    assistant turn (empty list for user turns) — used for dispute review
    (ADR-024 Risks: "Residual hallucination... tool_trace_json audit per turn").

    History is always rebuilt server-side from the last 12 rows for a session
    (ADR-024 Decision 4, AC-CHAT-10) — the client never supplies history.
    """

    session = models.ForeignKey(
        ChatSession,
        on_delete=models.CASCADE,
        related_name="messages",
    )
    role = models.CharField(max_length=10, choices=ChatRole.choices)
    content = models.TextField()
    tool_trace_json = models.JSONField(
        default=list,
        blank=True,
        help_text="List of {tool, input, output} dicts audited for this turn.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "chat message"
        verbose_name_plural = "chat messages"
        indexes = [
            models.Index(fields=["store", "session", "created_at"]),
        ]

    def __str__(self):
        return f"ChatMessage {self.pk} [{self.role}] session={self.session_id}"


class StoreChatSettings(StoreOwnedModel):
    """
    Per-store chat configuration + GDPR sub-processor opt-in (ADR-024 Decisions 2, 10).

    is_enabled defaults to False (chat is OFF by default). Enabling requires the
    admin UI to stamp subprocessor_terms_accepted_at + accepted_by in the SAME
    action as flipping is_enabled=True (enforced by the admin view/form, not by
    this model, so that re-disabling never clears the acceptance audit trail —
    disabling only ever flips is_enabled back to False).

    model_id_override: blank means "use the platform default" (settings.CHAT_MODEL_ID).

    Policy-page designations (see module docstring ASSUMPTION) back
    chat/tools.py's get_store_policy tool. All four are optional; an unset
    designation makes that policy kind resolve to a not-found tool result,
    which the system prompt instructs the model to handle by pointing the buyer
    to the contact form instead of inventing an answer.
    """

    is_enabled = models.BooleanField(default=False)
    subprocessor_terms_accepted_at = models.DateTimeField(null=True, blank=True)
    accepted_by = models.ForeignKey(
        settings.AUTH_USER_MODEL,
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="chat_settings_accepted",
    )
    model_id_override = models.CharField(
        max_length=100,
        blank=True,
        default="",
        help_text="Blank = use the platform default (settings.CHAT_MODEL_ID).",
    )
    daily_message_budget = models.PositiveIntegerField(
        default=500,
        help_text="Store-wide daily message cap (IP-independent), ADR-024 Decision 7.",
    )
    shipping_policy_page = models.ForeignKey(
        "pages.StaticPage",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text="StaticPage returned by get_store_policy(kind='shipping').",
    )
    refund_policy_page = models.ForeignKey(
        "pages.StaticPage",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text="StaticPage returned by get_store_policy(kind='refund').",
    )
    terms_policy_page = models.ForeignKey(
        "pages.StaticPage",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text="StaticPage returned by get_store_policy(kind='terms').",
    )
    contact_page = models.ForeignKey(
        "pages.StaticPage",
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name="+",
        help_text=(
            "StaticPage returned by get_store_policy(kind='contact'). When unset, "
            "falls back to the store's first published StaticPage of kind=CONTACT."
        ),
    )
    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "store chat settings"
        verbose_name_plural = "store chat settings"
        constraints = [
            models.UniqueConstraint(fields=["store"], name="unique_store_chat_settings"),
        ]

    def __str__(self):
        return f"StoreChatSettings [store={self.store_id}] enabled={self.is_enabled}"

    @property
    def is_opted_in(self) -> bool:
        """True when the store has both enabled chat AND accepted the sub-processor terms."""
        return bool(self.is_enabled and self.subprocessor_terms_accepted_at)
