# T016 — Known Risks

## Risk 1: Middleware DB hit on every request (MEDIUM)

`LocaleMiddleware` calls `resolve_locale()` which queries `StoreDomain` on
every non-excluded request. Under high load this adds one indexed DB lookup per
request. Mitigated by the indexed `host` column and Django's DB connection
pooling. No caching layer exists yet; this is acceptable for the current traffic
level but should be revisited when a caching layer (Redis, per-process cache) is
introduced.

## Risk 2: Reserved-slug rule breadth (LOW — PENDING HUMAN DECISION)

The slug validator rejects patterns matching `^[a-z]{2}(-[a-z]{2})?\Z`. This
blocks all two-letter and four-letter locale-like slugs (`en`, `fr`, `en-us`,
etc.), not only ISO 639-1 codes. Legitimate slugs such as `go`, `do`, or `if`
would be rejected. The decision on whether to tighten the rule to a strict ISO
639-1 list or leave the broader block in place is PENDING HUMAN DECISION.
Tracked in `specs/ecommerce_engine/11_uncertainties_to_validate.md`.

Consequence if broader block stays: a small set of common English words and
potential product slugs are permanently blocked. No security risk.

## Risk 3: Soft-deleted store visibility (RESOLVED)

Safety Agent originally blocked release on this issue (F1). Fixed: `resolve_locale`
filters `store__deleted_at__isnull=True`. After-bug regression test PROVEN
(test fails when both fixes are removed, passes with fixes in place).

## Risk 4: PLATFORM_APEX_DOMAIN missing in production (LOW)

If the environment variable is not set, the application will refuse to start
with `ImproperlyConfigured`. This is intentional fail-fast behavior, not a
silent failure. Deployment checklist includes this step explicitly.

## Human decisions required before next phase

| Decision | Owner | Urgency |
|---|---|---|
| Reserved-slug rule breadth: broader regex vs ISO 639-1 strict list | Human | Before permalink slug validation is used in production slug creation |
