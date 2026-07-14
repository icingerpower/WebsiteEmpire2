"""
Campaign validation functions (TICKET-027 / ADR-009 §XV-5, TICKET-021 / ADR-010 Q2).

Single validation layer for offer configuration and campaign activation.
Called from:
  - Campaign.clean() (all save paths)
  - Campaign admin save_model (belt-and-suspenders on the admin surface)
  - AbandonedCheckoutEmailStep.clean() (persistence boundary)

All functions raise django.core.exceptions.ValidationError on failure.
Never catch and swallow exceptions — let the error propagate so the caller
knows the operation was rejected (design-pattern-ideas §XIII).

FR-V1 (T039): validate_campaign_activation now collects ALL errors and raises
a single ValidationError with the full message list, not just the first error.
"""

from django.core.exceptions import ValidationError


def validate_offer_config(offer_type: str, config: dict) -> None:
    """
    Validate offer_config_json for a given offer_type archetype.

    Raises ValidationError if the config is missing required fields or has
    out-of-range values. Per-archetype schemas:

      related_products:  product_ids (non-empty list)
      buy_x_get_x:       buy_quantity (int >= 1), get_quantity (int >= 1),
                         product_id (int). No cap field (DECIDED 2026-07-04).
      storewide_discount: discount_pct (0 < float <= 100) OR discount_fixed (float > 0);
                          expires_in_days (int >= 1, default 30 if absent).
      one_click_funnel:  product_id (int).
      order_bump:        product_id (int).

    Unknown offer_type raises ValidationError.
    """
    # capture_window_minutes is a Campaign-level field (Campaign.capture_window_minutes).
    # Reject it in ALL per-step configs to enforce the persistence boundary (ADR-011 Q5 —
    # the capture window is a funnel-level fact, not a per-step setting; FR-S4).
    if "capture_window_minutes" in config:
        raise ValidationError(
            "'capture_window_minutes' must not appear in offer_config_json — "
            "the capture window is a Campaign-level field (Campaign.capture_window_minutes). "
            "Remove it from the step configuration (ADR-011 Q5 / FR-S4)."
        )

    if offer_type == "related_products":
        product_ids = config.get("product_ids")
        if not isinstance(product_ids, list) or len(product_ids) == 0:
            raise ValidationError(
                "related_products requires a non-empty 'product_ids' list."
            )
        # upsell_amount_cents required for off-session charge (FR-V1.5 / FR-S3).
        upsell_amount_cents = config.get("upsell_amount_cents")
        if not isinstance(upsell_amount_cents, int) or upsell_amount_cents <= 0:
            raise ValidationError(
                "related_products requires 'upsell_amount_cents' (integer > 0). "
                "This is the charge amount for the off-session upsell (FR-S3)."
            )

    elif offer_type == "buy_x_get_x":
        buy_qty = config.get("buy_quantity")
        get_qty = config.get("get_quantity")
        product_id = config.get("product_id")
        if not isinstance(buy_qty, int) or buy_qty < 1:
            raise ValidationError(
                "buy_x_get_x requires 'buy_quantity' (integer >= 1)."
            )
        if not isinstance(get_qty, int) or get_qty < 1:
            raise ValidationError(
                "buy_x_get_x requires 'get_quantity' (integer >= 1)."
            )
        if not isinstance(product_id, int):
            raise ValidationError(
                "buy_x_get_x requires 'product_id' (integer)."
            )
        # ADR-009 §5 (DECIDED 2026-07-04): buy_x_get_x has no cap field.
        # The free item is always 100% free. Reject any cap-related key so that
        # this schema constraint is enforced at the persistence boundary (§XV-5).
        _CAP_KEYS = frozenset({"cap", "cap_value", "free_value_cap", "max_free_value"})
        found_cap = _CAP_KEYS & config.keys()
        if found_cap:
            raise ValidationError(
                f"buy_x_get_x config must not include a cap field "
                f"({', '.join(sorted(found_cap))!r}) — "
                "the free item is always 100% free (ADR-009 §5, DECIDED 2026-07-04)."
            )
        # upsell_amount_cents required for off-session charge (FR-V1.5 / FR-S3).
        upsell_amount_cents = config.get("upsell_amount_cents")
        if not isinstance(upsell_amount_cents, int) or upsell_amount_cents <= 0:
            raise ValidationError(
                "buy_x_get_x requires 'upsell_amount_cents' (integer > 0). "
                "This is the charge amount for the off-session upsell (FR-S3)."
            )

    elif offer_type == "storewide_discount":
        discount_pct = config.get("discount_pct")
        discount_fixed = config.get("discount_fixed")
        if discount_pct is None and discount_fixed is None:
            raise ValidationError(
                "storewide_discount requires 'discount_pct' or 'discount_fixed'."
            )
        if discount_pct is not None:
            if not isinstance(discount_pct, (int, float)) or not (0 < discount_pct <= 100):
                raise ValidationError(
                    "storewide_discount 'discount_pct' must be a number with 0 < pct <= 100."
                )
        if discount_fixed is not None:
            if not isinstance(discount_fixed, (int, float)) or discount_fixed <= 0:
                raise ValidationError(
                    "storewide_discount 'discount_fixed' must be a positive number."
                )
        expires_in_days = config.get("expires_in_days", 30)
        if not isinstance(expires_in_days, int) or expires_in_days < 1:
            raise ValidationError(
                "storewide_discount 'expires_in_days' must be an integer >= 1 (default 30)."
            )
        # storewide_discount issues a coupon — no off-session charge, no upsell_amount_cents.

    elif offer_type == "one_click_funnel":
        product_id = config.get("product_id")
        if not isinstance(product_id, int):
            raise ValidationError(
                "one_click_funnel requires 'product_id' (integer)."
            )
        # upsell_amount_cents must be present and > 0.  _get_upsell_amount_cents() in
        # upsell_service.py raises UpsellUnavailableError when it is absent or zero,
        # which means every customer accept silently fails at runtime if this field is
        # missing (§XV-1 invisible failure).  Catch it here at the persistence boundary
        # (§XV-5) so the admin cannot activate a broken funnel (FR-V1.5 / FR-S3).
        upsell_amount_cents = config.get("upsell_amount_cents")
        if not isinstance(upsell_amount_cents, int) or upsell_amount_cents <= 0:
            raise ValidationError(
                "one_click_funnel requires 'upsell_amount_cents' (integer > 0)."
            )

    elif offer_type == "order_bump":
        product_id = config.get("product_id")
        if not isinstance(product_id, int):
            raise ValidationError(
                "order_bump requires 'product_id' (integer)."
            )
        # upsell_amount_cents required for off-session charge (FR-V1.5 / FR-S3).
        upsell_amount_cents = config.get("upsell_amount_cents")
        if not isinstance(upsell_amount_cents, int) or upsell_amount_cents <= 0:
            raise ValidationError(
                "order_bump requires 'upsell_amount_cents' (integer > 0). "
                "This is the charge amount for the off-session upsell (FR-S3)."
            )

    else:
        raise ValidationError(
            f"Unknown offer_type: {offer_type!r}. "
            "Valid types: related_products, buy_x_get_x, storewide_discount, "
            "one_click_funnel, order_bump."
        )


def validate_email_step(step) -> None:
    """
    Validate an AbandonedCheckoutEmailStep at the persistence boundary (ADR-010 Q2, §XV-5).

    Called from AbandonedCheckoutEmailStep.clean() and validate_campaign_activation.
    Raises ValidationError for any violation.

    Rules:
    - subject must be non-empty.
    - body_template must be non-empty.
    - send_delay_hours must be >= 1.
    - coupon_config_json: if non-empty, must have valid keys and values:
        value_type: 'percent' or 'fixed'
        value: number > 0
        expires_in_days: int >= 1 (default 30 when absent)
        No unknown keys are permitted.
    """
    if not (step.subject or "").strip():
        raise ValidationError("Email step subject must not be empty.")

    if not (step.body_template or "").strip():
        raise ValidationError("Email step body_template must not be empty.")

    # Defense-in-depth: reject template tags that can load code or cause
    # information disclosure.  The real protection is context serialization in
    # _serialize_campaign_context(); this validation is a belt-and-suspenders
    # guard at the persistence boundary (Safety B1 / §XV-5).
    _FORBIDDEN_TAGS = ("{% load ", "{%load ", "{% include ", "{%include ",
                       "{% debug", "{%debug", "{% exec", "{%exec")
    for field_name, field_val in (
        ("subject", step.subject or ""),
        ("body_template", step.body_template or ""),
    ):
        for tag in _FORBIDDEN_TAGS:
            if tag in field_val:
                raise ValidationError(
                    f"Email step {field_name} contains a forbidden template tag "
                    f"({tag.strip()!r}). "
                    "Forbidden tags: {% load %}, {% include %}, {% debug %}, {% exec %}."
                )

    delay = step.send_delay_hours
    if not isinstance(delay, int) or delay < 1:
        raise ValidationError(
            "Email step send_delay_hours must be an integer >= 1."
        )

    cfg = step.coupon_config_json or {}
    if cfg:
        _ALLOWED_COUPON_KEYS = frozenset({"value_type", "value", "expires_in_days"})
        unknown_keys = set(cfg.keys()) - _ALLOWED_COUPON_KEYS
        if unknown_keys:
            raise ValidationError(
                f"coupon_config_json contains unknown keys: {sorted(unknown_keys)!r}. "
                "Allowed keys: value_type, value, expires_in_days."
            )

        value_type = cfg.get("value_type")
        if value_type not in ("percent", "fixed"):
            raise ValidationError(
                "coupon_config_json 'value_type' must be 'percent' or 'fixed'."
            )

        value = cfg.get("value")
        if not isinstance(value, (int, float)) or value <= 0:
            raise ValidationError(
                "coupon_config_json 'value' must be a number > 0."
            )
        if value_type == "percent" and value > 100:
            raise ValidationError("Percent discount cannot exceed 100%.")

        expires_in_days = cfg.get("expires_in_days", 30)
        if not isinstance(expires_in_days, int) or expires_in_days < 1:
            raise ValidationError(
                "coupon_config_json 'expires_in_days' must be an integer >= 1 (default 30)."
            )


def _validate_step_graph(campaign) -> list:
    """
    Walk the funnel graph from campaign.entry_step; collect all errors (FR-V1).

    Returns a list of error strings. Caller is responsible for raising ValidationError.

    Uses iterative DFS with gray/black coloring (§XV-3 / ADR-009 §1 risks):
      gray  = currently on the DFS stack path (entering)
      black = fully processed (exiting)

    A back edge to a gray node is a cycle → adds error, stops that branch.
    A FK to a step in another campaign → adds error, skips that branch.
    Every visited step's offer_config_json is validated via validate_offer_config;
    errors are collected rather than raised immediately.

    This function is safe for campaign graphs with up to ~1000 steps; Python's
    default recursion limit is never hit because we use an explicit stack.
    """
    errors = []
    _GRAY = "gray"
    _BLACK = "black"
    colors: dict = {}

    # Stack entries: (step, is_enter)
    # is_enter=True  → first visit: mark gray, validate, push exit + push neighbors
    # is_enter=False → second visit: mark black (DFS exit)
    stack = [(campaign.entry_step, True)]

    while stack:
        step, is_enter = stack.pop()

        if is_enter:
            if colors.get(step.pk) == _BLACK:
                # Already fully processed via another path (diamond / fan-in) — skip.
                continue
            if colors.get(step.pk) == _GRAY:
                # Back edge: cycle detected. Record error but stop traversal here
                # to prevent infinite recursion.
                errors.append(
                    f"Cycle detected in the funnel graph involving step pk={step.pk}."
                )
                continue

            colors[step.pk] = _GRAY

            # Validate this step's offer configuration; collect errors.
            try:
                validate_offer_config(step.offer_type, step.offer_config_json)
            except ValidationError as exc:
                msgs = exc.messages if hasattr(exc, 'messages') else [str(exc)]
                for msg in msgs:
                    errors.append(f"Step #{step.pk}: {msg}")

            # Push the exit marker for this step.
            stack.append((step, False))

            # Push neighbors in reverse order so accept is visited before decline
            # (consistent DFS traversal order for deterministic error messages).
            for attr in ("decline_next_step", "accept_next_step"):
                next_step = getattr(step, attr)
                if next_step is None:
                    continue
                if next_step.campaign_id != campaign.pk:
                    errors.append(
                        f"Step pk={step.pk} has a branch FK ('{attr}') pointing to "
                        f"step pk={next_step.pk} which belongs to a different campaign."
                    )
                    # Do not follow a foreign-campaign branch.
                else:
                    stack.append((next_step, True))

        else:
            # DFS exit: mark this node as fully processed.
            colors[step.pk] = _BLACK

    return errors


def validate_campaign_activation(campaign) -> None:
    """
    Validate that a Campaign may be saved with is_active=True (FR-V1 — T039).

    Called from Campaign.clean() so all save paths (admin, API, management commands)
    go through the same validation function (persistence boundary, §XV-5).
    No-ops when is_active=False.

    FR-V1: collects ALL errors and raises a single ValidationError with the full
    message list. Previous behavior raised on the first error encountered.

    For funnel-type campaigns (campaign_type != 'abandoned_checkout'):
      - entry_step must be set.
      - entry_step must belong to this campaign.
      - Walk the branch graph from entry_step via accept_next_step / decline_next_step:
          * All branch FKs must point to steps of this same campaign.
          * No cycles (DFS with gray/black coloring).
          * Every reachable step's offer_config_json is validated via validate_offer_config.

    For abandoned_checkout campaigns (ADR-010 Q2):
      - No CampaignStep rows allowed (those are funnel-type only; ADR-009 §4).
      - At least one AbandonedCheckoutEmailStep required.
      - Every email step must pass validate_email_step.

    Raises:
        ValidationError: when any activation precondition is violated.
          Contains a list of all error messages (FR-V1).
    """
    if not campaign.is_active:
        return

    errors = []

    if campaign.campaign_type == "abandoned_checkout":
        # Reject if any CampaignStep rows exist (those belong to funnel-type campaigns).
        # Access steps only when the campaign has a PK (unsaved Campaign has no steps).
        #
        # ADR-031 addendum audit (TICKET-051): `campaign.steps` is a
        # reverse-FK related manager (CampaignStep.campaign -> Campaign) —
        # ADR-031 deliberately leaves those raising (only relation-bound M2M
        # managers were relaxed). `.exists()` used to silently bypass that
        # raise entirely; scoped explicitly now that it raises
        # unconditionally.
        from campaigns.models import CampaignStep

        if campaign.pk and CampaignStep.objects.for_store(campaign.store).filter(campaign=campaign).exists():
            errors.append(
                "An 'abandoned_checkout' campaign cannot have CampaignStep rows. "
                "Email steps are managed separately."
            )

        # Require at least 1 AbandonedCheckoutEmailStep (ADR-010 Q2).
        if campaign.pk:
            from campaigns.models import AbandonedCheckoutEmailStep
            email_steps = list(
                AbandonedCheckoutEmailStep.objects
                .for_store(campaign.store)
                .filter(campaign=campaign)
            )
            if not email_steps:
                errors.append(
                    "An 'abandoned_checkout' campaign must have at least one email step "
                    "before activation."
                )
            for step in email_steps:
                try:
                    validate_email_step(step)
                except ValidationError as exc:
                    msgs = exc.messages if hasattr(exc, 'messages') else [str(exc)]
                    errors.extend(msgs)

        if errors:
            raise ValidationError(errors)
        return

    # --- Funnel-type campaign ---
    if not campaign.entry_step_id:
        errors.append("entry_step must be set before activating a funnel campaign.")
    elif campaign.entry_step.campaign_id != campaign.pk:
        errors.append(
            f"entry_step (step pk={campaign.entry_step_id}) does not belong "
            f"to this campaign."
        )

    # Only validate the step graph if entry_step is present and valid.
    # _validate_step_graph requires a valid entry_step to start its traversal.
    if not errors:
        graph_errors = _validate_step_graph(campaign)
        errors.extend(graph_errors)

    if errors:
        raise ValidationError(errors)
