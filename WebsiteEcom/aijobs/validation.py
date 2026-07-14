"""
AI output validation pipeline (ADR-003 §6, TICKET-015).

Validates pre-extracted (field_id, content) pairs before persisting them as
AiJobOutput rows. The caller is responsible for extracting field values from
the raw AI response (e.g. Claude Messages API) and passing them here as a list.

Public API:
    validate_and_commit(job_run, field_outputs: list[tuple[str, str]]) -> bool

Checks (in order):
    field_id_dedup   — no duplicate field_id values in the input list; ERROR
    min_length       — each content >= 50 chars; ERROR
    max_length       — each content <= 10000 chars; WARNING (does not block)
    bracket_balance  — balanced { } [ ] in each field; ERROR
    length_ratio     — output >= 35% of source length (truncation guard); ERROR
                       Skipped if no 'source_content' key in job.input_payload.

Severity rules:
    ERROR   — if any check with this severity fails, the commit is blocked:
              zero AiJobOutput rows are written, run status set to ERROR.
    WARNING — if a check fails, the failure is recorded but does NOT block
              the commit; AiJobOutput rows are still written.

ADR-003 §6 — persistence gate:
    On all-ERROR-pass: AiJobValidation rows + AiJobOutput rows (one per field_id).
    On ERROR failure:  AiJobValidation rows only; zero AiJobOutput rows.
"""

import logging

from django.db import transaction
from django.utils import timezone

from aijobs.models import AiJobOutput, AiJobStatus, AiJobValidation

logger = logging.getLogger("aijobs.validation")


# ---------------------------------------------------------------------------
# Internal helpers — write one AiJobValidation row per check
# ---------------------------------------------------------------------------


def _write_validation(job_run, check_name: str, passed: bool, severity: str, details: dict):
    """Persist one AiJobValidation row for a single check result."""
    AiJobValidation(
        store=job_run.store,
        run=job_run,
        check_name=check_name,
        passed=passed,
        details_json=details,
        severity=severity,
    ).save()


# ---------------------------------------------------------------------------
# Individual checks
# Each check writes one AiJobValidation row and returns True (passed) / False (failed).
# ---------------------------------------------------------------------------


def _check_field_id_dedup(field_outputs: list, job_run) -> bool:
    """
    Detect duplicate field_id values in the input list.
    ERROR severity — duplicate field IDs indicate a malformed structured response.
    """
    field_ids = [fid for fid, _ in field_outputs]
    seen: set[str] = set()
    duplicates: list[str] = []
    for fid in field_ids:
        if fid in seen:
            duplicates.append(fid)
        seen.add(fid)

    passed = len(duplicates) == 0
    details: dict = {"duplicates": duplicates} if not passed else {}
    _write_validation(
        job_run,
        check_name="field_id_dedup",
        passed=passed,
        severity=AiJobValidation.SEVERITY_ERROR,
        details=details,
    )
    return passed


def _check_min_length(field_id: str, content: str, job_run) -> bool:
    """
    Catch empty or near-empty field content (< 50 chars).
    ERROR severity — too-short output is almost always a truncation or refusal.
    """
    actual = len(content)
    minimum = 50
    passed = actual >= minimum
    _write_validation(
        job_run,
        check_name="min_length",
        passed=passed,
        severity=AiJobValidation.SEVERITY_ERROR,
        details={"field_id": field_id, "actual": actual, "minimum": minimum},
    )
    return passed


def _check_max_length(field_id: str, content: str, job_run) -> bool:
    """
    Flag suspiciously long field content (> 10 000 chars).
    WARNING severity — does not block commit; recorded for human review.
    """
    actual = len(content)
    maximum = 10_000
    passed = actual <= maximum
    _write_validation(
        job_run,
        check_name="max_length",
        passed=passed,
        severity=AiJobValidation.SEVERITY_WARNING,
        details={"field_id": field_id, "actual": actual, "maximum": maximum},
    )
    return passed


def _check_bracket_balance(field_id: str, content: str, job_run) -> bool:
    """
    Count { } [ ] in the field content. Unbalanced brackets usually indicate
    truncated JSON or malformed structured output.
    ERROR severity (ADR-003 §6: bracket balance is a blocking check).
    """
    curly_ok = content.count("{") == content.count("}")
    square_ok = content.count("[") == content.count("]")
    passed = curly_ok and square_ok

    details: dict = {"field_id": field_id}
    if not passed:
        details.update(
            {
                "open_curly": content.count("{"),
                "close_curly": content.count("}"),
                "open_square": content.count("["),
                "close_square": content.count("]"),
            }
        )
    _write_validation(
        job_run,
        check_name="bracket_balance",
        passed=passed,
        severity=AiJobValidation.SEVERITY_ERROR,
        details=details,
    )
    return passed


def _check_length_ratio(field_id: str, content: str, source_content: str, job_run) -> bool:
    """
    Truncation guard: output must be >= 35% of source length (ADR-003 §6).
    Skipped (returns True without writing a row) when source is empty.
    ERROR severity — output shorter than 35% of source is almost certainly truncated.
    """
    source_len = len(source_content)
    if source_len == 0:
        return True  # No source to compare — skip silently.

    output_len = len(content)
    ratio = output_len / source_len
    minimum_ratio = 0.35
    passed = ratio >= minimum_ratio
    _write_validation(
        job_run,
        check_name="length_ratio",
        passed=passed,
        severity=AiJobValidation.SEVERITY_ERROR,
        details={
            "field_id": field_id,
            "output_length": output_len,
            "source_length": source_len,
            "ratio": round(ratio, 4),
            "minimum_ratio": minimum_ratio,
        },
    )
    return passed


# ---------------------------------------------------------------------------
# Core runner (called inside a savepoint by validate_and_commit)
# ---------------------------------------------------------------------------


def _run_validation(job_run, field_outputs: list) -> bool:
    """
    Execute all checks, write AiJobValidation rows, conditionally write
    AiJobOutput rows, and update job_run status.

    Returns True if commit succeeded (all ERROR checks passed), False otherwise.
    """
    has_error = False

    # ---- Global check: field_id uniqueness --------------------------------
    if not _check_field_id_dedup(field_outputs, job_run):
        has_error = True

    # ---- Source content for length-ratio check ----------------------------
    # Looked up from job.input_payload['source_content'] if present.
    source_content: str = ""
    try:
        source_content = job_run.job.input_payload.get("source_content", "") or ""
    except (AttributeError, TypeError):
        pass  # job not loaded or input_payload malformed — skip length_ratio

    # ---- Per-field checks -------------------------------------------------
    for field_id, content in field_outputs:
        if not _check_min_length(field_id, content, job_run):
            has_error = True
        _check_max_length(field_id, content, job_run)  # WARNING — never sets has_error
        if not _check_bracket_balance(field_id, content, job_run):
            has_error = True
        if source_content:
            if not _check_length_ratio(field_id, content, source_content, job_run):
                has_error = True

    # ---- Persist AiJobOutput rows only when all ERROR checks pass ---------
    # ADR-003 §6: on ERROR, nothing persisted.
    if not has_error:
        for field_id, content in field_outputs:
            AiJobOutput(
                store=job_run.store,
                run=job_run,
                field_id=field_id,
                content=content,
                is_accepted=True,
            ).save()

    # ---- Update run status -----------------------------------------------
    job_run.status = AiJobStatus.ERROR if has_error else AiJobStatus.DONE
    job_run.completed_at = timezone.now()
    job_run.save(update_fields=["status", "completed_at"])

    return not has_error


# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------


@transaction.atomic
def validate_and_commit(job_run, field_outputs: list) -> bool:
    """
    Run automated validation checks on the list of (field_id, content) pairs.

    If all ERROR-severity checks pass:
        → writes AiJobOutput rows (one per field_id).
        → writes AiJobValidation rows for every check.
        → sets job_run.status = DONE.
        → returns True.

    If any ERROR-severity check fails:
        → writes AiJobValidation rows (the audit trail) but NO AiJobOutput rows.
        → sets job_run.status = ERROR.
        → returns False.

    WARNING-severity check failures write validation rows but do not block commit.

    Never raises — all exceptions are caught, logged, and job_run is marked ERROR.
    The @transaction.atomic decorator wraps everything; a savepoint lets the except
    handler roll back the broken inner work and still write the ERROR status update.

    Args:
        job_run:       AiJobRun instance with .store and .job loaded (or loadable).
        field_outputs: list of (field_id, content) tuples — e.g.
                       [("title", "Great Widget"), ("description", "A long text…")]
    """
    sid = transaction.savepoint()
    try:
        result = _run_validation(job_run, field_outputs)
        transaction.savepoint_commit(sid)
        return result
    except Exception:
        logger.exception(
            "Unexpected error in validate_and_commit [job_run=%s]", job_run.pk
        )
        transaction.savepoint_rollback(sid)
        try:
            job_run.status = AiJobStatus.ERROR
            job_run.error_message = "Unexpected error during output validation."
            job_run.completed_at = timezone.now()
            job_run.save(update_fields=["status", "error_message", "completed_at"])
        except Exception:
            logger.exception(
                "Failed to mark job_run=%s as ERROR after validation crash", job_run.pk
            )
        return False
