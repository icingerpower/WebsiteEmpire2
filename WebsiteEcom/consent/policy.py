"""
Consent platform constants (ADR-025 D1/D2/D6, TICKET-048).

Frozen keys and platform-wide values — never per-store (P2/P4 decisions):
raising the policy version, the cookie name/lifetime, or the retention window
are platform-level product decisions, not store settings.
"""

# Bump ONLY for semantic changes (new category, new exempt reading) — never
# for copy edits (ADR-025 Risks: "policy_version bumps re-prompt every
# shopper platform-wide"). Any cookie stamped with a different version is
# treated as undecided by consent.state.get_consent() (fail-closed).
CONSENT_POLICY_VERSION = 1

# Category keys (ADR-025 D1). "necessary" always allowed, never toggled.
CATEGORY_NECESSARY = "necessary"
CATEGORY_ANALYTICS = "analytics"
CATEGORY_MARKETING = "marketing"

# All consent-requiring categories a shopper can grant/refuse independently
# (excludes "necessary", which is never a decision point, and the exempt
# audience-measurement row, which is governed by its own am_objected toggle).
CONSENT_CATEGORIES = (CATEGORY_ANALYTICS, CATEGORY_MARKETING)

# First-party cookie set exclusively by POST /_consent/ (ADR-025 D2).
CONSENT_COOKIE_NAME = "pradize_consent"

# 180 days (P2 — DECIDED human 2026-07-11): CNIL's 6-month re-prompt
# recommendation. Platform-level constant, not per-store (per-store intervals
# invite a race to the legal ceiling of 13 months).
CONSENT_COOKIE_MAX_AGE = 60 * 60 * 24 * 180

# 13 months rolling purge of ConsentRecord proof rows (P4 — DECIDED human
# 2026-07-11). 395 days ~= 13 months; a plain day-count constant is used
# instead of a relativedelta-based "13 calendar months" because
# python-dateutil is not a project dependency (requirements.txt) and adding
# one for a single day-math constant is not worth the approval overhead.
CONSENT_RECORD_RETENTION_DAYS = 395
