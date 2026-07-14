# Runbook: Enabling full custom-domain DKIM signing for a SenderDomain

**Applies to:** TICKET-040 / ADR-034 (per-store custom email sender domains).
**Audience:** Ops / whoever administers the outbound SMTP relay.
**Not application code** — nothing in this runbook is triggered automatically
by Django. It is a manual, per-domain deploy/ops step.

## Why this runbook exists

When a store's `SenderDomain` reaches `VERIFIED` in the admin, Pradize has
confirmed (via DNS TXT lookups):

1. The domain's SPF record includes `mail.pradize.com` (or whatever
   `SENDER_DOMAIN_PLATFORM_HOST` is configured to), so mail claiming to be
   `noreply@<their-domain>` sent through the platform's relay passes an SPF
   check.
2. A DKIM public key matching the platform-generated keypair is published at
   `<selector>._domainkey.<their-domain>`.

**What this does NOT do by itself:** make outbound mail for that domain
actually DKIM-signed with `d=<their-domain>`. Publishing the public key in DNS
is only half of DKIM — something must sign each outgoing message with the
matching **private key** before it reaches the wire. Django's `send_mail()`
calls straight into `EMAIL_BACKEND` (env-configured SMTP, see
`webecom/settings/production.py`); it does not sign messages itself (ADR-034
rejected adding a signing dependency in v1 — see the ADR's "SMTP relay / DKIM
signing" section for the reasoning).

Until this runbook's steps are performed for a given domain, outbound mail for
that store is:
- From-address: the verified custom address (`noreply@their-domain.com`) — done automatically.
- SPF: passes, via the platform's `include:` mechanism — done automatically.
- DKIM: signed with the **platform's own** `*.mail.pradize.com` key
  (platform-DKIM alignment), NOT `d=their-domain.com`. This is honest,
  functional email deliverability — it is just not "their domain cryptographically
  vouching for the message" the way full custom-domain DKIM would be.

## Retrieving the store's private key

The private key is stored encrypted at rest
(`SenderDomain.dkim_private_key`, `EncryptedCharField`/Fernet, `FERNET_KEY`
setting) and is **never** shown in any admin screen. To retrieve it for relay
configuration:

```python
# Django shell (manage.py shell), run by an operator with server access —
# never paste the output into a support ticket, chat, or log.
from emails.models import SenderDomain
d = SenderDomain.objects.get(domain="their-domain.com")
print(d.dkim_private_key)   # PEM, PKCS8 — transparently decrypted by EncryptedCharField
```

Treat this value exactly like any other private key material: it grants the
ability to sign mail as that domain. Do not store it outside the relay's own
secret storage.

## Configuring the relay

Steps depend on which `EMAIL_BACKEND`/relay is in production use:

- **Managed SMTP provider (SES / SendGrid / Postmark, etc.):** these products
  have a native "verify domain for sending" flow that includes DKIM signing
  once you add their provided CNAME/TXT records (a *different* mechanism than
  the platform's own SPF/DKIM records above) **or** they support "bring your
  own DKIM key" — upload the private key retrieved above through their
  dashboard/API for this specific sending domain.
- **Self-hosted Postfix + OpenDKIM:** add a `KeyTable`/`SigningTable` entry
  mapping `their-domain.com` to the selector (`SenderDomain.dkim_selector`,
  default `pradize1`) and a `.private` key file containing the PEM retrieved
  above. Reload `opendkim`. Verify with a real test send + `mail-tester.com`
  or equivalent.

## Verifying it worked

Send a real test email from the store (any transactional template) to a
DKIM-checking inbox or `mail-tester.com`, and confirm:
- `DKIM-Signature: d=their-domain.com` (not `mail.pradize.com`) in the headers.
- The signature validates (the checker reports DKIM: pass for that `d=`).

## Rollback

Removing the relay-side key/entry for a domain immediately reverts that
domain to platform-DKIM-alignment only — it does not affect `SenderDomain`'s
`VERIFIED` status (the DNS records this ADR checks are unrelated to relay
signing configuration) and requires no Django-side change.
