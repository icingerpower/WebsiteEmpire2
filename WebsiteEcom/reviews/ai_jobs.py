"""
AI job type registration for the reviews app (TICKET-023).

Registers the 'ai_product_review' job type in the aijobs plugin registry (ADR-003 §7).
Full build_prompt / validate_output / persist_output implementations are deferred to
the AI job orchestration phase (ADR-003). This stub registers the type so the scheduler
recognises it and existing AiJob rows with job_type='ai_product_review' are valid.

human_review_gate is True by default for this job type — AI-generated reviews must
be approved by a human moderator before being published (requires_human_review=True on AiJob).
"""

from aijobs.registry import JobTypeDefinition, register_job_type

AI_REVIEW_JOB_TYPE = "ai_product_review"


def _build_prompt(job):
    """Stub — full implementation deferred to AI orchestration phase."""
    raise NotImplementedError("ai_product_review build_prompt not yet implemented")


def _validate_output(raw_output, job):
    """Stub — full implementation deferred to AI orchestration phase."""
    return []


def _persist_output(job_run, outputs):
    """Stub — full implementation deferred to AI orchestration phase."""
    raise NotImplementedError("ai_product_review persist_output not yet implemented")


def _estimate_tokens(job):
    """Stub — returns a conservative default estimate."""
    return 500


register_job_type(
    JobTypeDefinition(
        job_type=AI_REVIEW_JOB_TYPE,
        display_name="AI Product Review",
        build_prompt=_build_prompt,
        validate_output=_validate_output,
        persist_output=_persist_output,
        estimate_tokens=_estimate_tokens,
        capabilities=["text"],
    )
)
