# Known Risks — T021: Abandoned Checkout Campaign

---

## R-001 (HIGH — production gate): GDPR legal basis for emailing non-purchasers

**Description:** The abandoned-checkout campaign emails customers who did not complete
a purchase. Depending on jurisdiction (EU GDPR, UK GDPR, CASL), this may require
explicit consent or a legitimate-interest assessment (LIA). No legal basis has been
confirmed for this use case.

**Status:** Open — tracked in `specs/ecommerce_engine/11_uncertainties_to_validate.md`.

**Gate:** The campaign type `abandoned_checkout` must NOT be activated in production
without a legal review confirming the applicable legal basis per store jurisdiction.
The feature can be deployed; it must not be enabled by store admins until legal
clearance is obtained.

**Owner:** Human (legal/policy decision — cannot be delegated to engineering).

---

## R-002 (HIGH — rollback): CampaignSession rows without order FK block reverse migration

**Description:** Once `scan_abandoned_checkouts` runs in production and creates
`CampaignSession` rows with `order=NULL`, rolling back migration 0003 requires
manually deleting those rows first. See `ROLLBACK_PLAN.md` §Rollback risks.

**Mitigation:** Stop beat before rollback; delete NULL-order sessions if count is
acceptable; then reverse migrate.

---

## R-003 (MEDIUM): Beat scan ±5-minute granularity may cause perceived late sends

**Description:** `scan_abandoned_checkouts` runs every 5 minutes. A send configured
for `send_delay_hours=1` may fire up to 5 minutes late. Documented in ADR-010 Q4 and
in the `CELERY_BEAT_SCHEDULE` comment in `webecom/settings/base.py`.

**Assessment:** Acceptable for hour-granularity send delays. Not acceptable if
sub-minute precision is ever required — would require a dedicated scheduler change.

---

## R-004 (MEDIUM): send_transactional_email credential-exfiltration class vulnerability

**Description:** `_serialize_campaign_context()` protects `send_campaign_email` in
`emails/service.py`. However, `send_transactional_email` in the same module passes
template context without equivalent serialization. A store admin with access to
transactional email templates could potentially exploit the same class of vulnerability.

**Status:** Tracked as a systemic improvement. Out of T021 scope per approved scope
definition. Requires a separate ticket.

---

## R-005 (MEDIUM): _RaisingQuerySet does not override .update()/.delete()

**Description:** `_RaisingQuerySet` raises `IsolationError` on iteration (e.g. list(),
for-in). It does NOT intercept `.update()` or `.delete()` calls, which silently bypass
the store-isolation guard. T021-B7 fixed the specific SENT status update via
`for_store(store).filter(pk=send_id).update(...)`. Other `.update()` or `.delete()`
calls on StoreOwnedModel querysets elsewhere in the codebase may silently bypass the
guard.

**Status:** Systemic improvement. Out of T021 scope. Requires a separate ticket to
override `.update()` and `.delete()` in `_RaisingQuerySet`.

---

## R-006 (LOW): Multi-language email steps not supported

**Description:** `AbandonedCheckoutEmailStep` stores a single copy of subject/body.
Stores with multiple active languages will send the same email text to all customers
regardless of their locale.

**Status:** Deferred. `AbandonedCheckoutEmailStepTranslation` model is the planned
solution (future ticket). Not a defect — the campaign works correctly for single-language
stores.

---

## R-007 (LOW): HMAC tokens in sent emails become dead links on rollback

**Description:** If T021 is rolled back after emails have been sent, all resume-cart
links in those emails will 404 (URL route removed). No action is possible — the email
has already been delivered.

**Assessment:** Acceptable. The link becoming inactive is a better user experience than
a broken system.

---

## Human decisions required before or at production activation

| Decision | Owner | Urgency |
|---|---|---|
| GDPR legal basis for abandoned-checkout emails | Human (legal/policy) | Before enabling campaign in production |
| Whether to add `AbandonedCheckoutEmailStepTranslation` and when | Human (product) | Before multilingual stores go live |
| Systemic fix for `_RaisingQuerySet.update()`/`.delete()` prioritization | Human (product) | Medium-term |
