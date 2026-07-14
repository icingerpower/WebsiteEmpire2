# Release Notes — Checkout (complete) + Static Pages / Contact & Quotation Forms

**Date:** 2026-07-10

## Summary

This release closes out the checkout area end-to-end and adds static pages (About, Terms,
Privacy Policy, etc.) with public contact and quotation forms — the last two major storefront
areas ahead of general availability.

## Checkout

- Address collection and validation, country-aware fields (ADR-017).
- Cart lifecycle: checkout state machine, idempotent order creation, stale-checkout
  garbage collection (ADR-016).
- Shipping service description text at the zone level (ADR-019).
- PayPal buyer-approval completion flow: approve → capture, with a signed forged-amount
  guard mirroring the existing Stripe defense (H1) and a Decimal-cents conversion fix (M1)
  so amount comparisons cannot drift by a cent (ADR-020, and
  `docs/security/CHECKOUT_PAYPAL_AMOUNT_VALIDATION.md`).
- Checkout language resolution: the shopper's browsing language now survives into
  `/checkout/`, `/cart/`, `/orders/.../thank-you/`, and the post-purchase upsell widget's
  reload, via `LocaleMiddleware`'s new session-write exemption list — closing a bug where
  every fixed-route request silently reset the buyer's language to the domain root (ADR-015
  addendum; regression `SF-LANG-UPSELL`).
- Translation activation (gettext wiring) for the storefront (ADR-021).

## Static pages + contact/quotation forms

- New `pages` app: admin-authored static pages (About, Terms, Privacy Policy, etc.) with
  per-language translations, publish/unpublish lifecycle, and store-scoped nav placement.
- Public contact form and store-level quotation request form, with honeypot + per-IP rate
  limiting + per-store notification-budget anti-spam defenses.
- SEO integration: canonical URLs, hreflang (with a policy-page exemption), sitemap
  inclusion, and — as of the fixes in this release — correct behavior when a page is
  unpublished and republished (translated permalinks now reactivate correctly) and when a
  translated page's slug changes (a 301 redirect is now created, matching the existing
  default-language behavior).
- (ADR-018.)

## Notable fixes shipped in this release (security + SEO audit follow-through)

- Reserved top-level slugs (`cart`, `checkout`, language codes, etc.) are now rejected at
  every write path that creates or renames a permalink — previously the check existed but
  was never actually invoked in production code, engine-wide (products, collections, static
  pages all benefit).
- Public contact/quotation form fields are now length-capped before being written to the
  database, preventing an unhandled 500 on the production PostgreSQL backend from
  over-length input on an unauthenticated endpoint.
- Production cache is now required to be Redis (fails loudly at startup if unconfigured),
  making the anti-spam rate limiter's atomicity guarantee actually true.
- Contact-form email notifications are now capped by a per-store daily budget, closing an
  email-bomb amplification path.
- The honeypot field was renamed away from a well-known bot-evasion name.
- Contact and store-level-quotation rate limits now use distinct scopes.
- The PayPal capture webhook now validates amount and currency before marking an order PAID
  (previously unchecked, unlike the sibling Stripe handler).

## What is explicitly NOT in this release

- T030 pixel-provider consumption of the purchase-guard flag (depends on the still-open
  MEDIUM-1 fix in `CHECKOUT_BATCH_2_AUDIT.md`).
- PayPal off-session upsell charging (Vault) — gated on merchant onboarding.
- A consolidated `DEPLOYMENT_HARDENING.md` — deploy-level conditions are itemized in
  `KNOWN_RISKS.md` and `RELEASE_CHECKLIST.md` instead.
- Any UI/theme visual changes — this release is backend logic, forms, and SEO plumbing only.

See `RELEASE_CHECKLIST.md` for full evidence per checklist item, `KNOWN_RISKS.md` for the
consolidated risk list, and `ROLLBACK_PLAN.md` for the rollback procedure.
