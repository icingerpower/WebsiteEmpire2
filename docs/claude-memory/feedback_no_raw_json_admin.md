---
name: feedback-no-raw-json-admin
description: Admin must never make a user hand-type JSON; replace *_json textareas with UI editors or hide inert ones
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-03T06:15:41.037Z
---

Cédric's standing rule (2026-09-03): the admin must NEVER require a user to type raw
JSON. Any `JSONField` / `*_json` field exposed as a plain textarea makes the user
"clueless what to do" and is unacceptable. Triggered by the campaign Add form's
"Targeting rules json" textarea.

**Why:** store admins aren't developers; a raw `{}` textarea gives zero affordance
about what's possible or required. (Worse when the field is inert — see below.)

**How to apply:** for every raw-JSON admin field, pick one:
- **Hide** if the field is inert/Phase-1 no-op or internal (e.g. `Campaign.targeting_rules_json`
  is ignored by `campaigns/service.py`; `Pixel.config_json` unread in v1) — don't build UI for a dead field.
- **CsvListWidget** (`core/widgets.py`) for simple string lists (tags, country codes, keywords).
- **Structured ModelForm** (fixed known keys → real fields, merge-preserve in save) — pattern:
  `CampaignStepAdminForm` / `AbandonedCheckoutEmailStepForm` in `campaigns/admin.py`.
- **Builder widget** (variable-length rules/rows) — pattern: `SmartRulesBuilderWidget`
  (`catalog/admin_forms.py`, CSP-safe: Textarea subclass + template + json_script + adminui JS,
  degrades to raw, server-side `clean_*` gate). Also `PermissionGridFormMixin`, `JsonMultipleChoiceField`.
Also type-gate forms so irrelevant fields don't show (e.g. abandoned-email fields on a funnel campaign).

Full inventory of remaining raw-JSON admin fields + per-field treatment was produced 2026-09-03
(campaign targeting/pixel config = hide; badge segments/size-guide grading/variant measurements =
builder; coupon config = structured; signup_tags/match_keywords/country_codes = CSV). Relates to
[[project_admin_redesign]].
