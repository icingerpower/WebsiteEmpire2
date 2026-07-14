"""
AI job type registration for the campaigns app (TICKET-027, ADR-009).

Registers the 'campaign_step_translation' job type in the aijobs plugin registry
(ADR-003 §7). Mirrors catalog/ai_jobs.py.

Translations run at highest priority (default_priority=100, per ADR-003 §1) and
auto-publish by default (requires_human_review=False on AiJob).

Full build_prompt / validate_output / persist_output implementations are deferred
to the AI job orchestration phase. This stub registers the type so the scheduler
recognises it and existing AiJob rows with job_type='campaign_step_translation'
are valid.
"""

from aijobs.registry import JobTypeDefinition, register_job_type

CAMPAIGN_STEP_TRANSLATION_JOB_TYPE = "campaign_step_translation"


def _build_prompt(job):
    """Stub — full implementation deferred to AI orchestration phase."""
    raise NotImplementedError("campaign_step_translation build_prompt not yet implemented")


def _validate_output(raw_output, job):
    """Stub — returns no errors (auto-publish path)."""
    return []


def _persist_output(job_run, outputs):
    """Stub — full implementation deferred to AI orchestration phase."""
    raise NotImplementedError("campaign_step_translation persist_output not yet implemented")


def _estimate_tokens(job):
    """Conservative default estimate for a campaign step translation job."""
    return 500


register_job_type(
    JobTypeDefinition(
        job_type=CAMPAIGN_STEP_TRANSLATION_JOB_TYPE,
        display_name="Campaign Step Translation",
        build_prompt=_build_prompt,
        validate_output=_validate_output,
        persist_output=_persist_output,
        estimate_tokens=_estimate_tokens,
        capabilities=["text"],
        # Translations are highest priority per ADR-003 §1.
        default_priority=100,
    )
)
