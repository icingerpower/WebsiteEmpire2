# ADR-034: Per-store custom email sender domains

**Status:** ACCEPTED (human, 2026-07-11 — `dnspython` dependency APPROVED, automated DNS-verification path (B1) proceeds; the manual-verification fallback (B3) is no longer needed). Implements `TICKET-040` (Phase 3, P3, complexity **M**). Extends `DECIDED Settings-C1` v1 (shared platform sender `noreply@mail.pradize.com`, store sets display-name + reply-to only).

**Depends on:** TICKET-022 (`emails/service.py`, `EmailTemplate`), ADR-006 §7 encrypted-credential pattern (`payments/fields.py::EncryptedCharField`, Fernet — reused as-is, no new crypto dependency), ADR-008 (`stores.StoreDomain` — the plain-`Model`-with-globally-unique-column pattern this ADR follows for the same reason).

## Decision

Ship `SenderDomain` as a **plain `models.Model`** (store `ForeignKey`, **not** `StoreOwnedModel`), an explicit verification state machine, platform-generated DKIM keys, and a single `resolve_from_email(store)` function that both send paths call. v1 delivers a **verified custom From-address with SPF pass and platform-DKIM alignment**; full custom-domain DKIM signing is a documented ops/relay task, not Django code.

## Context

TICKET-040 asks for: DNS verification (SPF/DKIM), "verification polling with explicit status states — never inferred", fallback to the platform sender until verified, and a per-store sender switch. Today `emails/service.py::send_transactional_email` and `send_campaign_email` both read `from_email = getattr(settings, 'DEFAULT_FROM_EMAIL', ...)` — a single flat setting, no per-store concept at all. `EMAIL_BACKEND` is an env-configured SMTP relay (`webecom/settings/production.py`); Django never touches the wire protocol below `send_mail()`.

## Options considered

**A. `SenderDomain(StoreOwnedModel)`** (as originally sketched). Rejected: `domain` must be **globally unique** (two stores cannot claim the same DNS name) and a Celery beat task must scan **all** `PENDING` rows across every store to re-check DNS. `StoreScopedManager.get_queryset()` raises on exactly that unscoped access, forcing either `cross_store_unsafe()` sprinkled through the verification task (an isolation smell for a table that isn't tenant-isolated data in the first place) or contorting the model to fit a base class built for tenant-isolated *data*. `StoreDomain` (ADR-008) already solved this identical shape — plain `Model`, `store` FK, globally-unique `host` — for the identical reason (request-routing needs a global domain→store map). `SenderDomain` copies that precedent exactly.

**B. DNS verification mechanism** — no stdlib DNS resolver exists in Python, so three options were weighed:
  - **B1. `dnspython` dependency** — a Celery beat task performs real TXT lookups (SPF include + DKIM selector match), matching "verification polling with explicit status states" as written. **New production dependency — DECIDED/APPROVED (human, 2026-07-11)** (joins `requirements.txt`'s `redis`/`psycopg2-binary`/`anthropic` approved-dependency list, same "Approved <date> (reason)" convention).
  - **B2. Verification-by-email-roundtrip** — proves the store owner controls a mailbox at the domain, not that SPF/DKIM TXT records exist. Wrong tool: it would let "Verified" mean something the sending pipeline doesn't actually check. Rejected as dishonest.
  - **B3. Manual platform-admin verification only** — a super-admin looks up `dig`/`nslookup` output themselves and flips a toggle. Zero new dependency, zero new failure surface, but does not scale (this is meant to be self-serve) and contradicts "verification polling" in the ticket text.

## Chosen option

A (plain `Model`, ADR-008 pattern) + **B1, DECIDED (human, 2026-07-11)** — `dnspython` is approved, so the Developer ticket ships the automated DNS-verification path against B1 directly; B3 (super-admin "Mark verified" fallback) is no longer needed and is retained here only as documented history of the option considered.

### Model — `SenderDomain`
`store` FK (CASCADE), `domain` (globally unique, lowercased/normalized in `save()` exactly like `StoreDomain.host`), `status` (`UNVERIFIED` → `PENDING` → `VERIFIED` | `FAILED`, explicit enum, never inferred from absence — §XV-1/§3), `dkim_selector` (default `"pradize1"`), `dkim_private_key` (`EncryptedCharField`, reusing `payments/fields.py` — no new crypto dependency), `dkim_public_key` (plain text — it is meant to be published, not secret), `verified_at`, `last_checked_at`, `last_check_error`, `sender_local_part` (default `"noreply"`, store-editable — the address's mailbox part).

### Keypair + DNS records shown in admin
On `SenderDomain` creation, generate a 2048-bit RSA keypair with `cryptography` (already a dependency, ADR-006 §7). The admin screen renders three copy-paste records:
- **SPF** — TXT: `v=spf1 include:mail.pradize.com ~all` (admin UI must show how to *merge* into an existing SPF record, never instruct "overwrite" — a wrong merge silently breaks the store's other mail).
- **DKIM** — TXT at `<selector>._domainkey.<domain>`: `v=DKIM1; k=rsa; p=<dkim_public_key>`.
- **DMARC** — informational only, `_dmarc` TXT with `p=none` suggested starting policy. Not verified by v1 (advisory guidance, no gate).

### Verification flow (state machine, §XV-3)
`UNVERIFIED` (initial) → store owner clicks "Verify" → `PENDING` (task enqueued/polling) → beat task re-checks every N minutes, up to a capped attempt budget (e.g. 30 attempts over 3 days — DNS propagation is slow) → `VERIFIED` (SPF include present AND DKIM TXT content matches) or `FAILED` (budget exhausted or explicit record mismatch, `last_check_error` populated). Network/timeout errors during a check **retry**, they never flip status by themselves (§XV-1 — a transient resolver failure must not read as "you failed verification"). `FAILED`/`VERIFIED` are both re-triggerable back through `PENDING` by the store owner. A **periodic re-check of already-`VERIFIED`` rows** (weekly) demotes to `FAILED` with an alert if records disappear — otherwise a domain that changes DNS ownership after verification keeps platform trust indefinitely (flagged **PENDING product decision**: exact re-check cadence).

### Sending path change
New `emails/sender.py::resolve_from_email(store) -> str` is the **single** resolution function (§XV-4) called from both `send_transactional_email` and `send_campaign_email`, replacing the two identical `getattr(settings, 'DEFAULT_FROM_EMAIL', ...)` lines:
- `store` has a `SenderDomain` with `status=VERIFIED` → `formataddr((store.email_display_name, f"{domain.sender_local_part}@{domain.domain}"))`. Reply-to is unchanged (still the store's existing Settings-C1 reply-to field).
- Otherwise → `settings.DEFAULT_FROM_EMAIL` (today's exact v1 behavior). Pure additive fallback — no regression to existing stores.
AC-183 ("rendering unchanged across senders") stays valid untouched: `resolve_from_email` only changes the envelope `From`, never subject/body rendering.

### SMTP relay / DKIM signing — honest v1 scope
Publishing the DNS TXT record does **not**, by itself, make outbound mail DKIM-signed — something must sign the message with the matching private key before it hits the wire. Two ways to get there were assessed:
- **Sign in Django** (custom `EmailBackend` wrapping a DKIM-signing library around `send_mail`) — a second new dependency in the same ADR, more code on the send-critical path, larger failure surface. **Rejected for v1** — own future ticket if needed.
- **Sign at the relay** (Postfix/managed SMTP provider — SES/Sendgrid/Postmark all support per-sending-domain verification+signing natively) — this is how production mail infrastructure actually achieves DKIM; it is a **deploy/ops configuration step** (add the domain + install the generated private key at the relay), not application code, consistent with `EMAIL_BACKEND` already being env-configured SMTP.
**v1 promise, stated plainly:** a verified custom From-address, SPF pass via the `include:` mechanism, and platform-DKIM alignment (the existing `*.mail.pradize.com` signature). **Full custom-domain DKIM (`d=customdomain.com`) requires the relay to be configured with the store's private key — an ops/runbook item**, tracked on the release checklist, not delivered by this ticket's Django code. This is a partial promise and must be described to store owners as such (no "your domain is 100% DKIM-verified" claim in the UI copy).

## Why

Matches the ticket's explicit ask (polling, explicit states, fallback, per-store switch) while being honest about what a Django app can and cannot deliver for DKIM signing without owning the outbound MTA. Reuses two already-approved primitives (`EncryptedCharField`, the `StoreDomain` global-uniqueness shape) instead of inventing new ones, minimizing new surface to: one model, one resolver function, one Celery beat task, one dependency decision.

## Risks

- **Spoofing surface (mandatory Safety Agent gate before release):** verifying a domain grants the platform the ability to send mail that looks like it's from that domain. DNS-based verification (control of the zone) is the standard proxy for domain control — but the "Mark verified" manual override (B3 fallback, or any future support tooling) must be restricted to super-admins and **audit-logged**, since it bypasses the actual DNS check.
- **Domain takeover after verification:** a store could lose control of a domain (expired registration, DNS change) after being marked `VERIFIED`. Mitigated by the weekly re-check demotion above — exact cadence (ADR-034:P2) remains PENDING; not part of the 2026-07-11 decision (only the `dnspython` dependency, ADR-034:P1, was decided).
- **R-1 (accepted risk, WAVE 4 Safety Agent audit, docs/security/WAVE4_AUDIT.md):** the weekly re-check cadence above means a domain that loses control after being marked `VERIFIED` stays trusted (spoof-capable `From`) for up to ~7 days until the next demotion pass — a residual post-verification spoofing window. **Accepted for v1.** The release note / runbook must state this window explicitly, and the re-check cadence should be a documented, tunable setting (not a hardcoded "weekly") so ops can shorten it (e.g. to daily) for a domain known to be sensitive.
- **`dnspython` is a new production dependency** (pure Python, actively maintained, no C extension) — DECIDED/APPROVED (human, 2026-07-11), joining `redis`/`psycopg2-binary`/`anthropic` in `requirements.txt`'s approved-dependency list. (B3 manual verification is no longer needed as a fallback.)
- **DNS propagation delay** (24–48h typical) means "Verify" will not succeed immediately; UI must show `PENDING` distinctly from `FAILED` so store owners don't think verification broke.

## Rollback strategy

Purely additive: `SenderDomain` rows with no `VERIFIED` row fall back to the current v1 behavior automatically (the resolver's `else` branch). Feature can be fully disabled by a settings flag (`SENDER_DOMAINS_ENABLED=False`) that short-circuits `resolve_from_email` to always return the platform default — instant revert, no data migration, no impact on any other table.

## Tests required

1. State machine: every legal transition (`UNVERIFIED→PENDING→VERIFIED`, `→FAILED`, `FAILED→PENDING` retry) and that no other transition is reachable.
2. `resolve_from_email`: verified store → custom address; unverified/no-row store → platform default; demoted-mid-send race (domain flips to `FAILED` between resolution and send) does not crash `send_mail`.
3. DNS check task (mocked `dnspython` responses): match → `VERIFIED`; content mismatch → `FAILED` + `last_check_error` set; timeout/network error → status unchanged, retried, never silently swallowed (Test Integrity rule).
4. Uniqueness: a second store claiming an already-claimed `domain` is rejected at the DB layer.
5. AC-183 regression: rendered subject/body byte-identical regardless of which sender path is used.
6. Weekly re-check demotion: a `VERIFIED` row whose DNS records later disappear is demoted to `FAILED` with an alert, not left silently `VERIFIED` (§XV-1).
7. Safety: the manual "Mark verified" override is denied to non-super-admin roles and writes an audit row.
