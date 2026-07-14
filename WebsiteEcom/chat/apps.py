"""
Chat app config (ADR-024, TICKET-038).

ready() registers the chat widget SlotProvider into the ADR-012 slot registry
under the "chat_launcher" slot (ADR-012 D9 / spec 15 TH-045) — the same
self-registration pattern every other storefront integration app uses
(pixels, reviews, etc.). See chat/slot_provider.py.
"""

from django.apps import AppConfig


class ChatConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "chat"
    verbose_name = "AI sales chat"

    def ready(self):
        from storefront.slots import register

        from chat.slot_provider import ChatWidgetSlotProvider

        register(ChatWidgetSlotProvider())
