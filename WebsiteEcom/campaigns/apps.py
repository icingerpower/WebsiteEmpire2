from django.apps import AppConfig


class CampaignsConfig(AppConfig):
    default_auto_field = "django.db.models.BigAutoField"
    name = "campaigns"
    verbose_name = "Campaigns"

    def ready(self):
        import campaigns.signals  # noqa: F401 — registers CampaignStep post_save signal
        import campaigns.ai_jobs  # noqa: F401 — registers campaign_step_translation job type
