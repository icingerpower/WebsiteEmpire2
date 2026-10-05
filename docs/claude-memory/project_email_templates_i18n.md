---
name: project-email-templates-i18n
description: Store email templates seeded per-store on create + auto-translated via AiJob CLI (ADR-065)
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-04T06:18:09.156Z
---

Email templates for Pradize (WebsiteEcom), shipped 2026-09-04 (ADR-065, `b546924`).

- Registry `emails/templates_registry.py` (`EmailTemplateId` TextChoices) is the single source of truth for which templates exist; `_DEFAULT_SUBJECTS` derived from it. `store_invitation`/`password_reset`/`abandoned_checkout_email` are non-seedable/non-translatable.
- Seeded per-store on Store create (`emails/seeding.py`, idempotent get_or_create, byte-identical to the file defaults); existing stores backfilled by migration `emails/0007`; operator command `queue_missing_email_translations` controls translation spend.
- Per-language content: `EmailTemplateTranslation` child model (store, template, lang_code), migration `emails/0006`. Auto-queued translation on source edit / new StoreLanguage via `aijobs.service.create_job`, sub_type `email_template_translation` (`emails/ai_jobs.py`), run by the existing CLI runner; auto-published. Kill switch `EMAIL_TEMPLATE_AUTO_TRANSLATE_ENABLED` (default True).
- Send path `emails/service.py send_transactional_email(locale=...)`: order.checkout_language → store default; published translation → source row → file default.
- **Safety-critical**: translated content is rendered as Django templates but the email context is pre-serialized to scalars (`_serialize_campaign_context`) — no settings/request/Model in scope. The validator is SYMMETRIC (translation tag multiset must MATCH source; can't add `{% %}`/`{{ }}`), and the send path falls back to the source template if a published translation fails to render. Do not weaken either without re-audit. See [[feedback_no_raw_json_admin]].

Note: the 500 on the store-admin email-template add page (fieldsets listed `store` after the form dropped it) was fixed separately in `29dca30`. Relates to [[project_pipeline_architecture]].
