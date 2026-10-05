---
name: feedback-translation-cli
description: "All customer-visible content must be translated by AI via CLI (not direct API), using the existing AiJob system"
metadata: 
  node_type: memory
  type: feedback
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
---

All customer-visible content — including email subjects/bodies, campaign offer text, CTA button labels, and any other user-facing strings — must be translated via the existing **AiJob CLI runner** (not direct Anthropic API calls).

**Why:** The user explicitly stated this as a rule. The WebsiteEmpire C++ project uses `ClaudeRunner` which spawns the `claude` CLI as a QProcess, sends JSON `{id, source}` objects, receives `{fieldId: translatedText}`. In WebsiteEcom, the same principle applies via the `aijobs` app.

**How to apply:**
- Any new model with customer-visible text fields (email templates, campaign step offer_config_json, CTA labels, etc.) must have a corresponding translation mechanism using `AiJob` (type `"translation"`, `default_priority=100`).
- Pattern: mirror `ProductTranslation`/`CollectionTranslation` — add an `ai_job` FK and a translation model or inline translation fields.
- Never call the Anthropic API directly for content translation — always go through the AiJob system so the CLI runner picks it up.
- Translation jobs are created automatically when content is saved (via signal or admin `save_model`).

See `catalog/ai_jobs.py` for the translation job type registration. See `WebsiteEmpire2/WebsiteAspire/launcher/ClaudeRunner.h` and `WebsiteEmpire2/WebsiteEmpireLib/website/pages/PageTranslator.h` for the C++ CLI runner reference.
