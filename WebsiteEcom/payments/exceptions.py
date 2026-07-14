"""
Payment-domain exceptions (H2 safety fix — ambiguous charge outcomes).

AmbiguousChargeOutcome:
    Raised by a connector when a charge attempt ended in an UNKNOWN state —
    the network call to the processor failed (timeout, connection reset) AFTER
    the request may already have been received and executed by the processor.

    This is fundamentally different from a decline: a decline is a definitive
    "no money moved" answer from the processor, while an ambiguous outcome
    means the customer MAY have been charged.  Treating an ambiguous outcome
    as a decline loses money-state (charged customer, FAILED row, undelivered
    goods → chargebacks).  Callers must:

    1. Attempt recovery via retrieve_charge_by_idempotency_key(idempotency_key).
    2. If recovery cannot confirm the outcome, persist the charge row as
       ChargeStatus.AMBIGUOUS_OUTCOME and leave the surrounding business flow
       UNFINALIZED so the capture-window watchdog can reconcile it later.
    3. Never silently convert this exception into success=False (§XIII).
"""


class AmbiguousChargeOutcome(Exception):
    """
    A processor charge ended in an unknown state (network failure mid-call).

    Attributes:
        idempotency_key: the idempotency key that was sent with the charge
            attempt.  Recovery code uses it to look the charge up on the
            processor side (the same key is also stored in the charge
            metadata by the upsell service).
    """

    def __init__(self, message: str, idempotency_key: str = ""):
        super().__init__(message)
        self.idempotency_key = idempotency_key
