from django.apps import AppConfig


class ReviewsConfig(AppConfig):
    name = "reviews"

    def ready(self):
        import reviews.signals  # noqa: F401 — wire post_save signal
        import reviews.ai_jobs  # noqa: F401 — register AI job type
