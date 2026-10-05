---
name: project-review-moderation
description: Review moderation feature — Phase A (moderation+reply+storefront) DONE; Phase B (review/reply translation) remaining
metadata:
  type: project
---

Review moderation for Pradize (WebsiteEcom), Phase A shipped 2026-09-06
(commits bd14a79, c5cb122, 98449ee).

**Locked product decisions:**
- HIDDEN review: shown on storefront as a placeholder revealing ONLY reviewer name
  + date + the reason (policy message); no stars/title/body. EXCLUDED from the
  average rating + summary counts.
- Merchant reply: ONE per review, stored inline on Review (`reply_body`,
  `reply_author_name`, `replied_at`, `replied_by`); author defaults to
  `StoreReviewSettings.default_reply_author_name` → else `store.email_display_name
  or store.name`, editable per reply.

**Phase A (DONE):**
- `Review`: HIDDEN status added to `ReviewStatus`; inline reply fields + `has_reply`.
  `StoreReviewSettings`: `default_reply_author_name`, `hidden_review_policy_message`
  (+ `DEFAULT_HIDDEN_REVIEW_POLICY_MESSAGE` constant + resolver helpers). Migration
  reviews/0003.
- `ReviewAdmin` per-row actions (ADR-052 pattern, CSP-safe, moderation-stamped):
  Approve/Hide/Reply (reply = a dedicated form view). Hover preview column
  (`.pa-review-peek`, CSS-only) + "View on storefront" link (product page +
  `#reviews` anchor). `catalog/services/storefront_links.py::resolve_object_default_permalink_slug`
  (request-cached).
- Storefront: `reviews/storefront.py` renders approved + hidden(placeholder),
  average = approved-only; `review_widget.html` reply block + hidden placeholder;
  `product.html` `id="reviews"` anchor.

**Phase B (DONE 2026-09-07, ADR-070):** auto-translate approved reviews AND
merchant replies into every enabled store language via AiJob. Commits d96605e
(impl T-REVTR-1..5), 0cde6f9 (tests T-REVTR-6 + fr .po), 216d54d (Safety
hardening). Design in `docs/adr/ADR-070-review-and-reply-translation.md`.
- ONE `ReviewTranslation(store, review, lang_code)` model — translated
  title/body/reply_body + TWO fingerprints (review vs reply, independent
  lifecycle); reply_author_name NOT translated. `Review.source_lang` added
  (per-review source — a de store translates FROM de, an en store FROM en).
  Migration reviews/0004.
- NEW AiJob job type `review_translation` (NOT the `ai_product_review` stub,
  which is review *generation*). One job per (review × target lang), only stale
  parts, NO reviewer PII in payload/prompt (source is already-public APPROVED
  text = C1). Source text wrapped in DATA delimiters (prompt-injection defense).
- Trigger: post_save on Review gated APPROVED + flag; fans out to enabled langs
  EXCEPT the review's own source_lang; per-part staleness + flood guard; reply
  edit re-queues reply part only. Operator-run backfill command
  `queue_missing_review_translations`.
- Storefront: per-part display_* fallback to original; LEGAL disclosure
  "Automatically translated from {source}" shown on translated parts only.
  Auto-publish (requires_human_review=False).
- Admin: reviews changelist column is the ADR-053 "View ▾" menu
  (build_view_menu_html gained an `anchor` kwarg → product page #reviews).
- **KILL SWITCH:** `REVIEW_TRANSLATION_ENABLED` (settings, DEFAULT FALSE) gates
  the trigger, the backfill, AND resolve_product_reviews — flag off = instant
  storefront-wide fallback to originals (Safety LOW-1 fix). Enabling is Cédric's
  call (spend + public auto-publish of machine translations).
Relates to [[project_admin_polish_sweep]], [[project_email_templates_i18n]],
[[feedback_translation_cli]].
