"""
Signals for the campaigns app (TICKET-027, ADR-009).

Translation AiJob trigger (post_save on CampaignStep):
  When a CampaignStep is saved with non-empty title, description, or cta_label
  in offer_config_json, create AiJob entries (type 'campaign_step_translation')
  for each configured StoreLanguage that does not yet have a CampaignStepTranslation
  with status='published'. Mirrors the pattern in catalog/signals.py.

  The signal uses aijobs.service.create_job() — the only creation path for AiJobs.
  All imports are deferred to avoid circular import issues at module load time.
"""

import logging

from django.db.models.signals import post_save
from django.dispatch import receiver

logger = logging.getLogger(__name__)


@receiver(post_save, sender="campaigns.CampaignStep")
def _trigger_translation_jobs_on_step_save(sender, instance, created, **kwargs):
    """
    Emit AiJob translation jobs for a saved CampaignStep.

    Fires when the step has translatable content in offer_config_json
    (title, description, or cta_label keys). One job is created per StoreLanguage
    that does not yet have a published CampaignStepTranslation for this step.

    Idempotent: jobs are only created for languages that lack a 'published'
    translation. Re-saving the step without changing content does not create
    duplicate jobs if published translations already exist.

    Only creates jobs when:
    - The step's campaign store has at least one StoreLanguage configured.
    - At least one of title / description / cta_label is present in offer_config_json.
    """
    # Deferred imports to avoid circular import at module load time.
    from aijobs.models import AiJob, AiJobStatus
    from aijobs.service import create_job
    from campaigns.ai_jobs import CAMPAIGN_STEP_TRANSLATION_JOB_TYPE
    from campaigns.models import CampaignStepTranslation
    from stores.models import StoreLanguage

    config = instance.offer_config_json or {}
    has_translatable_content = any(
        config.get(key) for key in ("title", "description", "cta_label")
    )
    if not has_translatable_content:
        return

    store = instance.store

    # Fetch all language codes configured for this store.
    # StoreLanguage is not a StoreOwnedModel — filter directly on store FK.
    lang_codes = list(
        StoreLanguage.objects.filter(store=store).values_list("lang_code", flat=True)
    )
    if not lang_codes:
        return

    # Find languages that already have a published translation for this step.
    published_lang_codes = set(
        CampaignStepTranslation.objects.for_store(store)
        .filter(step=instance, status="published")
        .values_list("lang_code", flat=True)
    )

    # Non-terminal statuses: job is queued or in progress — no new job needed.
    _NON_TERMINAL = [AiJobStatus.NOT_STARTED, AiJobStatus.IN_PROGRESS]

    # Create a translation job for each language that lacks a published translation
    # AND does not already have a pending/in-progress job (flood guard).
    for lang_code in lang_codes:
        if lang_code in published_lang_codes:
            continue

        # Skip if a non-terminal job already exists for this (step, lang_code).
        # This prevents job flooding when a step is saved multiple times before any
        # job reaches a terminal state (DONE / ERROR / SKIPPED).
        if AiJob.objects.for_store(store).filter(
            job_type=CAMPAIGN_STEP_TRANSLATION_JOB_TYPE,
            target_model="campaigns.CampaignStep",
            target_id=instance.pk,
            lang=lang_code,
            status__in=_NON_TERMINAL,
        ).exists():
            logger.debug(
                "Skipping translation job for step=%s lang=%s: non-terminal job exists",
                instance.pk,
                lang_code,
            )
            continue

        # §XV-1: silent failure is forbidden. If AiJob creation fails (e.g. an
        # infrastructure or registry problem), log and re-raise so the admin save
        # surfaces the error rather than swallowing it.
        create_job(
            store=store,
            job_type=CAMPAIGN_STEP_TRANSLATION_JOB_TYPE,
            target_model="campaigns.CampaignStep",
            target_id=instance.pk,
            input_payload={
                "step_id": instance.pk,
                "offer_type": instance.offer_type,
                "offer_config": config,
                "lang_code": lang_code,
            },
            lang=lang_code,
            requires_human_review=False,
            created_by="trigger",
        )
