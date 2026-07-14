"""
SlotProvider for the storefront chat widget (ADR-024 Decision 9, ADR-012 D9).

Registered into the "chat_launcher" slot from chat/apps.py ready() — the same
self-registration pattern every other storefront integration uses. Rendered
via {% render_slot "chat_launcher" %} (storefront/templates/storefront/base.html).

Slot key is "chat_launcher", not "chat" (ADR-012 D9 / spec 15 TH-045 reserve
this name; the original "chat" key was an unapproved deviation corrected on
2026-07-11 per the ADR-024 addendum — see "Human decisions (2026-07-11)" item 2).

is_enabled() gates on exactly the three ADR-024 Decision 9 conditions: store
opted in (is_enabled AND subprocessor_terms_accepted_at set) AND kill switch
off. Quota-exhaustion is intentionally NOT checked here (that would add a
StoreAiQuota query to every single storefront page load) — the widget always
renders when opted-in, and its JS discovers a quota/rate-limit/cap refusal the
first time it POSTs /chat/session/ or /chat/message/, showing the "assistant
unavailable" state at that point instead (never hiding silently — ADR-024
Decision 9 / §XV-1).

Suppressed during theme preview two ways, deliberately (defense in depth):
  1. "chat_launcher" is listed in storefront.templatetags.storefront_tags.
     PREVIEW_SUPPRESSED_SLOTS, so render_slot() itself never calls this
     provider's render() during preview.
  2. render() below also checks theme_ctx.is_preview directly, in case this
     provider is ever invoked outside render_slot() (e.g. a future direct
     call, a test, or a second render path) — never rely on the caller alone
     to suppress a preview render (ADR-024 addendum, LOW note follow-up).
"""

from django.conf import settings
from django.template.loader import render_to_string

from chat.models import StoreChatSettings
from storefront.slots import SlotProvider


class ChatWidgetSlotProvider(SlotProvider):
    slot = "chat_launcher"
    key = "chat_widget"
    name = "AI sales assistant chat"

    def is_enabled(self, store) -> bool:
        if settings.CHAT_KILL_SWITCH:
            return False
        chat_settings = StoreChatSettings.objects.for_store(store).first()
        return bool(chat_settings and chat_settings.is_opted_in)

    def render(self, context) -> str:
        theme_ctx = context.get("theme_ctx")
        if getattr(theme_ctx, "is_preview", False):
            return ""
        request = context.get("request")
        return render_to_string(
            "storefront/partials/chat_widget.html",
            {"request": request},
            request=request,
        )
