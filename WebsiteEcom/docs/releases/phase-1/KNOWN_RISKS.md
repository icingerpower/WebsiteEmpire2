# Known Risks — Phase 1

> Compiled by Release Manager Agent. Initial: 2026-07-03. Updated: 2026-07-03 (2nd invocation — all blockers closed).
> Fast-follow items (deferred, not blocking) are separated from open blockers.

---

## Open blockers

None. All three blockers from the first Release Manager invocation are now closed.

### B1 — Missing migration files (CLOSED 2026-07-03)

Three metadata-only migration files generated and committed:
`aijobs/0004_alter_aijob_job_type.py`, `discounts/0004_alter_discountcode_times_used.py`,
`payments/0005_alter_organizationrule_fallback_rule_and_more.py`.
`makemigrations --check` exits 0. No DB schema impact.

### B2 — No Spec Reviewer 3rd pass approval document (CLOSED 2026-07-03)

`docs/releases/phase-1/SPEC_REVIEW_APPROVAL.md` created. Verdict: APPROVED.
All BLOCKERs and MAJORs resolved. Three MINORs accepted as fast-follows (R8, R9, R10).

### B3 — No Safety Agent CLEAR TO RELEASE document (CLOSED 2026-07-03)

`docs/releases/phase-1/SAFETY_CLEAR_TO_RELEASE.md` created. Verdict: CLEAR TO RELEASE.
C1/H1/H2/M2/L2 CLOSED. M1/M3/M4/L1/L3/L4 as named fast-follow tickets with Phase 2 gates.
`docs/security/SECURITY_CHECKLIST_upsell_capture_window.md` updated to APPROVED (14/14 boxes checked).

---

## Fast-follow risks (accepted, not blocking Phase 1 completion)

### M1 — Webhook order–account cross-org check

**Severity:** Medium (low for Phase 1; super-admin provisioned)
**Description:** A webhook for Order X from Organization A could theoretically be
processed against Organization B's processor account if routing changes occur between
authorization and webhook delivery.
**Mitigation timing:** Before T028 (upsell capture-window) ships in Phase 2.

### M3 — Per-email coupon concurrency at scale

**Severity:** Medium (low for Phase 1 traffic volume)
**Description:** The `ON CONFLICT … DO UPDATE … WHERE use_count < per_email_limit`
pattern is correct but may see contention at flash-sale traffic levels.
**Mitigation timing:** Before high-volume launch in Phase 2.

### M4 — Client-controlled beacon fields (analytics-only)

**Severity:** Medium (no SQLi; analytics data only)
**Description:** Beacon endpoint accepts client-asserted `product_id`, `collection_id`,
etc. without server-side validation against the store's actual catalog. This cannot
corrupt financial data but can pollute analytics reports.
**Mitigation timing:** Before Phase 2 analytics launch.

### L1 — EncryptedCharField silent decrypt on bad key

**Severity:** Low (masks misconfiguration)
**Description:** If `FERNET_KEY` is changed and old encrypted values are read,
`cryptography.fernet.InvalidToken` is raised at read time rather than at startup.
The failure is loud (exception, not silent) but the root cause (key change) may
take time to diagnose.
**Mitigation timing:** Before first production ProcessorAccount credential is stored.

### L3 — Anonymous session storage amplification

**Severity:** Low (storage only)
**Description:** Each anonymous visitor gets a session row in the `django_session`
table. High-traffic bots or scrapers can amplify session table size.
**Mitigation timing:** Before public storefront launch.

### L4 — No startup FERNET_KEY validation

**Severity:** Low (fail-loud at first use, not at startup)
**Description:** Missing `FERNET_KEY` raises `ImproperlyConfigured` at the first
`EncryptedCharField` access rather than at Django startup.
**Mitigation timing:** Add to `AppConfig.ready()` before production deployment.

---

## Architectural open items (PENDING, not blocking Phase 1)

These items are tracked in `specs/ecommerce_engine/11_uncertainties_to_validate.md`:

| Item | Blocks | Status |
|------|--------|--------|
| AF-C4 — send-delay field placement in abandoned-checkout wizard | T021 finish | Open |
| AF-C1 — per-store vs org-wide permission matrix | T047 | Open |
| Designer theme proposals (11:#11) | T029 (3 themes) | Open |
| Analytics column denominators / attribution window (Part A) | T013 finish | Open |
| Org-split × method-strategy layer composition (Part E) | T020 finish | Open |
| Domain→language→country model (11:#9) | T016/T024 | Open |
| AI chat technology (11:#10) | T038 | Open |
| French bookkeeping VAT spec (11:#12) | T043 | Open |
| Gift-card value modes / jurisdictional expiry | T045 | Open |
| GDPR: non-purchaser email, popup buyer data, PII erasure | T021/T035/T011 | Open |
