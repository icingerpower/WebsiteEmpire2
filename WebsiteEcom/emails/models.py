"""
Emails app models: EmailTemplate, SentEmail.

Design decisions (TICKET-022, ADR-001):
- EmailTemplate is store-scoped — each store can override a system template
  (e.g. order_confirmation) with custom subject/body. Falls back to the
  platform default (file-based) if no store-specific template exists.
  This follows ADR-001 §3b: platform masters in files, per-store overrides in DB.

- template_id is a stable string identifier (e.g. 'order_confirmation') that
  must never change once rows exist — it is the lookup key joining templates
  to triggers.

- SentEmail is an immutable audit log. Admin must not allow add or change —
  only view. It records the final rendered subject (not the template) so the
  log remains readable even after templates change.

- order FK on SentEmail uses SET_NULL so deleting an order does not destroy
  the email audit trail (compliance requirement).

- status values: 'sent' (accepted by SMTP), 'failed' (send_mail raised or
  returned error), 'bounced' (set later by webhook, Phase 3).

- SentEmail idempotency: the signal handlers check
  SentEmail.objects.filter(order=order, template_id=...).exists() before
  sending, preventing duplicate emails across retries.

- SenderDomain / SenderDomainManualVerification (TICKET-040, ADR-034): per-store
  custom email sender domains with DNS (SPF/DKIM) verification. See the
  SenderDomain docstring below for the full state machine and design rationale.
"""

from django.conf import settings
from django.core.exceptions import ValidationError
from django.core.validators import RegexValidator
from django.db import models
from django.utils import timezone

from core.models import StoreOwnedModel
from payments.fields import EncryptedCharField


class EmailTemplate(StoreOwnedModel):
    """
    Per-store override of a system email template.

    When a store has an EmailTemplate row matching the template_id, it is
    rendered instead of the platform default file-based template.

    subject and body_html are rendered as Django template strings — they may
    contain {{ variable }} and {% tag %} syntax.

    body_text is optional — when blank, the plain-text part falls back to the
    file-based .txt template or is omitted from the multipart email.

    is_active controls whether this override is in use. Deactivating it causes
    the service to fall back to the platform default without deleting the row.
    """

    template_id = models.CharField(
        max_length=100,
        help_text=(
            "Stable identifier matching the trigger (e.g. 'order_confirmation'). "
            "Never change once data exists."
        ),
    )
    subject = models.CharField(
        max_length=255,
        help_text="Subject line; may contain Django template variables.",
    )
    body_html = models.TextField(
        help_text="HTML email body; rendered as a Django template.",
    )
    body_text = models.TextField(
        blank=True,
        help_text=(
            "Plain-text email body (optional). When blank, falls back to the "
            "file-based .txt template or the HTML body is sent only."
        ),
    )
    is_active = models.BooleanField(
        default=True,
        help_text="When False, the platform default template is used instead.",
    )

    class Meta:
        verbose_name = 'email template'
        verbose_name_plural = 'email templates'
        unique_together = [('store', 'template_id')]

    def __str__(self):
        return f"{self.template_id} ({self.store})"


class SentEmailStatus(models.TextChoices):
    SENT = 'sent', 'Sent'
    FAILED = 'failed', 'Failed'
    BOUNCED = 'bounced', 'Bounced'


class SentEmail(StoreOwnedModel):
    """
    Immutable audit log of every transactional email sent.

    Records are written by send_transactional_email() on every attempt,
    including failures.  status=failed + error_message captures the reason.

    Used for:
    - Idempotency: signal handlers check existence before re-sending.
    - Support: store admins can see which emails were sent for an order.
    - Debugging: error_message captures send_mail exceptions.

    Admin: read-only (view only — no add or change permissions).
    """

    template_id = models.CharField(
        max_length=100,
        help_text="Template identifier matching the trigger.",
    )
    recipient_email = models.EmailField()
    subject = models.CharField(
        max_length=255,
        help_text="Final rendered subject (not the template string).",
    )
    order = models.ForeignKey(
        'orders.Order',
        null=True,
        blank=True,
        on_delete=models.SET_NULL,
        related_name='sent_emails',
        help_text='Associated order (null for non-order emails like welcome).',
    )
    sent_at = models.DateTimeField(auto_now_add=True)
    status = models.CharField(
        max_length=20,
        choices=SentEmailStatus.choices,
        default=SentEmailStatus.SENT,
    )
    error_message = models.TextField(
        blank=True,
        help_text="Exception message when status=failed.",
    )

    class Meta:
        verbose_name = 'sent email'
        verbose_name_plural = 'sent emails'
        indexes = [
            models.Index(fields=['store', 'sent_at']),
            models.Index(fields=['order', 'template_id']),
        ]

    def __str__(self):
        return f"{self.template_id} → {self.recipient_email} ({self.status})"


# ---------------------------------------------------------------------------
# SenderDomain (TICKET-040, ADR-034)
# ---------------------------------------------------------------------------


class SenderDomainStatus(models.TextChoices):
    """
    Explicit verification state (ADR-034 §"Verification flow", design-pattern
    lesson §XV-3: state is explicit, never inferred from absence).
    """

    UNVERIFIED = "unverified", "Unverified"
    PENDING = "pending", "Pending verification"
    VERIFIED = "verified", "Verified"
    FAILED = "failed", "Failed"


SENDER_LOCAL_PART_VALIDATOR = RegexValidator(
    r"^[A-Za-z0-9._%+-]+$",
    "Mailbox-safe characters only (letters, digits, and . _ % + -). "
    "Spaces, '@', and control characters are not allowed.",
)


def validate_no_crlf(value):
    """
    Explicit, defense-in-depth CR/LF rejection (F-3, docs/security/
    WAVE4_AUDIT.md) for SenderDomain.sender_local_part, which is
    interpolated directly into the From header's mailbox part
    (`f"{sender_local_part}@{domain}"` -> formataddr(...)). Django's own
    send_mail -> sanitize_address / forbid_multi_line_headers already
    raises BadHeaderError on CR/LF in a header at send time (that is why
    F-3 was rated LOW, not a live injection), and
    SENDER_LOCAL_PART_VALIDATOR's anchored character class already
    excludes CR/LF implicitly — this second, explicit check exists so the
    persistence boundary (§XV-5) rejects the value up front with a clear
    message, and so a future relaxation of the charset regex cannot
    silently reopen a CR/LF path without a human noticing this check.
    """
    if "\r" in value or "\n" in value:
        raise ValidationError("Must not contain line breaks (CR/LF).")


class SenderDomain(models.Model):
    """
    Per-store custom email sender domain (TICKET-040, ADR-034).

    Deliberately a **plain `models.Model`** — NOT `StoreOwnedModel`. `domain`
    must be **globally** unique (two stores cannot claim the same DNS name,
    exactly like `stores.StoreDomain`/ADR-008) and the periodic beat tasks
    (`emails/tasks.py`) must scan `PENDING`/`VERIFIED` rows across *every*
    store. `StoreScopedManager.get_queryset()` raises on that kind of
    unscoped access, so this model mirrors `StoreDomain`'s plain-`Model` shape
    for the identical reason (ADR-034 "Options considered" A).

    State machine (explicit, never inferred — §XV-3). Legal transitions only:
        UNVERIFIED -> PENDING            (store owner clicks "Verify")
        PENDING    -> VERIFIED           (DNS check: SPF + DKIM both match)
        PENDING    -> FAILED             (attempt/time budget exhausted, or an
                                          explicit DNS content mismatch)
        FAILED     -> PENDING            (store owner retries)
        VERIFIED   -> PENDING            (store owner re-triggers verification)
        VERIFIED   -> FAILED             (weekly re-check: records disappeared)
    All transitions go through mark_pending()/mark_verified()/mark_failed()
    below (guarded by `_LEGAL_TRANSITIONS`) — nothing else may assign
    `status` directly, so every transition is auditable and exhaustive.

    `force_verify()` is the one deliberate exception: the super-admin-only,
    audited "Mark verified" manual override (ADR-034 Option B3 / "Risks" —
    verifying a domain grants the platform the ability to send mail that
    looks like it is from that domain, so this bypass is a spoofing-adjacent
    action). It is callable from any status and is only ever invoked by
    `emails.admin.SenderDomainSuperAdmin.force_verify_manual`, which writes
    the required `SenderDomainManualVerification` audit row.

    Keys: `dkim_private_key` is `EncryptedCharField` (Fernet, reusing
    `payments/fields.py` — ADR-006 §7, no new crypto primitive) and is never
    exposed in the admin. `dkim_public_key` is plain text — it is meant to be
    published in DNS, not secret.

    v1 scope, stated plainly (ADR-034 "SMTP relay / DKIM signing — honest v1
    scope"): a verified custom From-address, SPF pass via the `include:`
    mechanism, and platform-DKIM alignment. Publishing the DKIM public key
    does **not**, by itself, make outbound mail DKIM-signed for this custom
    domain — the relay must additionally be configured with the private key,
    which is an ops/runbook step (`docs/runbooks/sender-domain-dkim-signing.md`),
    not Django code. Never claim "fully DKIM-verified" in UI copy.
    """

    _LEGAL_TRANSITIONS = {
        SenderDomainStatus.UNVERIFIED: {SenderDomainStatus.PENDING},
        SenderDomainStatus.PENDING: {SenderDomainStatus.VERIFIED, SenderDomainStatus.FAILED},
        SenderDomainStatus.FAILED: {SenderDomainStatus.PENDING},
        SenderDomainStatus.VERIFIED: {SenderDomainStatus.PENDING, SenderDomainStatus.FAILED},
    }

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.CASCADE,
        related_name="sender_domains",
    )
    domain = models.CharField(
        max_length=253,  # RFC 1035 FQDN limit, matches StoreDomain.host
        unique=True,
        help_text=(
            "Fully qualified domain to send email from (e.g. shop.example.com). "
            "Globally unique across all stores."
        ),
    )
    status = models.CharField(
        max_length=10,
        choices=SenderDomainStatus.choices,
        default=SenderDomainStatus.UNVERIFIED,
    )
    sender_local_part = models.CharField(
        max_length=64,
        default="noreply",
        validators=[SENDER_LOCAL_PART_VALIDATOR, validate_no_crlf],
        help_text=(
            "Mailbox part of the From address, e.g. 'noreply' in "
            "noreply@shop.example.com. Mailbox-safe characters only "
            "(letters, digits, and . _ % + -)."
        ),
    )
    dkim_selector = models.CharField(
        max_length=63,
        default="pradize1",
        help_text="DKIM selector — published at '<selector>._domainkey.<domain>'.",
    )
    dkim_private_key = EncryptedCharField(
        blank=True,
        default="",
        help_text=(
            "Platform-generated RSA private key (PEM, PKCS8), encrypted at rest. "
            "Never displayed in admin."
        ),
    )
    dkim_public_key = models.TextField(
        blank=True,
        default="",
        help_text=(
            "Base64 DER public key body (no PEM headers) — published verbatim "
            "in the DKIM TXT record's 'p=' tag. Not secret."
        ),
    )
    verification_attempts = models.PositiveIntegerField(
        default=0,
        help_text="Number of DNS check attempts since the last mark_pending().",
    )
    pending_since = models.DateTimeField(
        null=True,
        blank=True,
        help_text="When this domain last entered PENDING — anchors the attempt/time budget.",
    )
    verified_at = models.DateTimeField(null=True, blank=True)
    last_checked_at = models.DateTimeField(null=True, blank=True)
    last_check_error = models.TextField(
        blank=True,
        default="",
        help_text="Human-readable detail from the most recent DNS check (any status).",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "sender domain"
        verbose_name_plural = "sender domains"

    def __str__(self):
        return f"{self.domain} ({self.get_status_display()})"

    def save(self, *args, **kwargs):
        # Normalize exactly like StoreDomain.host (ADR-008) — same NFD/case/
        # scheme/port trap class the design-pattern lessons warn about.
        domain = self.domain or ""
        if "://" in domain:
            domain = domain.split("://", 1)[1]
        domain = domain.split(":")[0]
        self.domain = domain.lower().rstrip(".")
        # Defense-in-depth (F-3, docs/security/WAVE4_AUDIT.md): strip stray
        # CR/LF and surrounding whitespace from both header-adjacent fields
        # even on paths that bypass ModelForm.full_clean() (shell, data
        # migrations, management commands) — the real gate against
        # malformed values is SENDER_LOCAL_PART_VALIDATOR / validate_no_crlf
        # above, which every admin save still runs through full_clean().
        self.domain = "".join(self.domain.split())
        if self.sender_local_part:
            self.sender_local_part = self.sender_local_part.strip().replace("\r", "").replace("\n", "")
        if not self.pk and not self.dkim_private_key:
            self._generate_dkim_keypair()
        super().save(*args, **kwargs)

    def _generate_dkim_keypair(self):
        from .dkim_keys import generate_dkim_keypair

        private_pem, public_b64 = generate_dkim_keypair()
        self.dkim_private_key = private_pem
        self.dkim_public_key = public_b64

    # --- DNS records shown in admin (ADR-034 "Keypair + DNS records") -----

    @property
    def expected_spf_record(self):
        platform_host = getattr(settings, "SENDER_DOMAIN_PLATFORM_HOST", "mail.pradize.com")
        return f"v=spf1 include:{platform_host} ~all"

    @property
    def expected_dkim_record_name(self):
        return f"{self.dkim_selector}._domainkey.{self.domain}"

    @property
    def expected_dkim_record_value(self):
        return f"v=DKIM1; k=rsa; p={self.dkim_public_key}"

    @property
    def expected_dmarc_record_name(self):
        return f"_dmarc.{self.domain}"

    @property
    def expected_dmarc_record_value(self):
        # Informational only — not verified in v1 (ADR-034).
        return "v=DMARC1; p=none"

    # --- Explicit state transitions (§XV-3) --------------------------------

    def _transition(self, new_status):
        allowed = self._LEGAL_TRANSITIONS.get(self.status, set())
        if new_status not in allowed:
            raise ValueError(
                f"Illegal SenderDomain transition: {self.status!r} -> {new_status!r}"
            )
        self.status = new_status

    def mark_pending(self):
        """UNVERIFIED|FAILED|VERIFIED -> PENDING. Resets the attempt budget."""
        self._transition(SenderDomainStatus.PENDING)
        self.pending_since = timezone.now()
        self.verification_attempts = 0
        self.last_check_error = ""
        self.save()

    def mark_verified(self):
        """PENDING -> VERIFIED. DNS check matched (SPF include + DKIM content)."""
        self._transition(SenderDomainStatus.VERIFIED)
        self.verified_at = timezone.now()
        self.last_check_error = ""
        self.save()

    def mark_failed(self, reason: str):
        """PENDING|VERIFIED -> FAILED. `reason` is persisted to last_check_error."""
        self._transition(SenderDomainStatus.FAILED)
        self.last_check_error = reason
        self.save()

    def force_verify(self):
        """
        Manual super-admin override (ADR-034 Option B3) — sets VERIFIED
        directly from ANY status, bypassing both the DNS check and the
        `_LEGAL_TRANSITIONS` guard. This is intentional: it is the documented
        "someone looked at dig/nslookup output themselves" escape hatch, not
        part of the automated state machine.

        Callers MUST write a SenderDomainManualVerification audit row and
        MUST restrict this to super-admins — enforced in
        emails.admin.SenderDomainSuperAdmin.force_verify_manual, not here,
        so this method never silently runs without an audit trail.
        """
        self.status = SenderDomainStatus.VERIFIED
        self.verified_at = timezone.now()
        self.last_check_error = "Manually verified by super-admin override (bypasses DNS check)."
        self.save()


class SenderDomainManualVerification(models.Model):
    """
    Immutable audit log for SenderDomain.force_verify() (ADR-034 "Risks" —
    the manual override bypasses the actual DNS check, so every use must be
    traceable to a specific super-admin and timestamp). Mirrors the
    read-only-audit-log pattern used by payments.DecisionLog and
    consent.ConsentRecord: no add/change/delete via the admin UI beyond the
    override action itself creating rows.
    """

    sender_domain = models.ForeignKey(
        SenderDomain,
        on_delete=models.CASCADE,
        related_name="manual_verifications",
    )
    performed_by = models.ForeignKey(
        settings.AUTH_USER_MODEL,
        null=True,
        on_delete=models.SET_NULL,
        related_name="+",
    )
    previous_status = models.CharField(max_length=10, choices=SenderDomainStatus.choices)
    performed_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "sender domain manual verification"
        verbose_name_plural = "sender domain manual verifications"
        ordering = ["-performed_at"]

    def __str__(self):
        return f"{self.sender_domain_id} manually verified by {self.performed_by_id} at {self.performed_at}"
