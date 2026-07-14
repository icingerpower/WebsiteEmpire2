"""
Job-type plugin registry (ADR-003 §7).

The registry is the authority for job-type capabilities and routing.
Adding a new job type = one call to register_job_type().

AiJobType enum is kept for DB compatibility with existing rows, but the
registry — not the enum — determines what each job type can do and which
runner handles it.

Usage:
    from aijobs.registry import register_job_type, get_job_type, JobTypeDefinition

    register_job_type(JobTypeDefinition(
        job_type="product_description",
        display_name="Product Description",
        build_prompt=my_build_fn,
        validate_output=my_validate_fn,
        persist_output=my_persist_fn,
        estimate_tokens=my_estimate_fn,
        capabilities=["text"],
    ))
"""

from dataclasses import dataclass, field
from typing import Callable


@dataclass
class JobTypeDefinition:
    """
    Descriptor for one AI job type.

    Fields:
        job_type         — stable string identifier, e.g. 'product_description'.
                           Must match the value stored in AiJob.job_type for this type.
        display_name     — human-readable name for admin UIs and dashboards.
        build_prompt     — callable(job: AiJob) -> str; constructs the prompt sent to AI.
        validate_output  — callable(raw_output: str, job: AiJob) -> list[str];
                           returns a list of error messages (empty = valid).
        persist_output   — callable(job_run: AiJobRun, outputs: list[AiJobOutput]) -> None;
                           applies accepted output to the target object.
        estimate_tokens  — callable(job: AiJob) -> int; estimates input token count
                           for quota pre-check.
        capabilities     — list of required runner capabilities,
                           e.g. ['text'], ['image_gen'], ['command_exec'].
                           The scheduler uses this to select an appropriate runner.
        default_priority — default scheduling priority for jobs of this type.
                           Higher values are processed first. 0 = lowest priority.
                           create_job() uses this when no explicit priority is supplied.
    """

    job_type: str
    display_name: str
    build_prompt: Callable
    validate_output: Callable
    persist_output: Callable
    estimate_tokens: Callable
    capabilities: list[str] = field(default_factory=list)
    default_priority: int = 0


_REGISTRY: dict[str, JobTypeDefinition] = {}


def register_job_type(defn: JobTypeDefinition) -> None:
    """
    Register a job type definition.

    Call once at module level in each job-type module. Import the module
    somewhere in AppConfig.ready() to ensure registration happens at startup.

    Raises ValueError if the job_type is already registered — this is a
    programming error (duplicate module import or double-registration), not
    a data error.
    """
    if defn.job_type in _REGISTRY:
        raise ValueError(
            f"Job type '{defn.job_type}' is already registered. "
            f"Each job_type string must be registered exactly once."
        )
    _REGISTRY[defn.job_type] = defn


def get_job_type(job_type: str) -> JobTypeDefinition:
    """
    Retrieve a registered JobTypeDefinition by its job_type string.

    Raises KeyError if the job_type is not registered. Callers should guard
    against unregistered types (e.g. legacy DB rows whose module was removed).
    """
    return _REGISTRY[job_type]


def all_job_types() -> list[JobTypeDefinition]:
    """Return all registered job type definitions, in registration order."""
    return list(_REGISTRY.values())
