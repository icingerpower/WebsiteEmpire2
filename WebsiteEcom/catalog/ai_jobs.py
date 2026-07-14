"""
AI job type registration for the catalog app (TICKET-024).

Registers the 'translation' job type in the aijobs plugin registry (ADR-003 §7).

Translations run at highest priority (ADR-003 §1) and auto-publish by default
(requires_human_review=False on AiJob — ADR-003 §8).

Sub-job types ('sub_type' key in input_payload):
  'product'     — Product title, description, seo_title, seo_description.
  'collection'  — Collection title, description, seo_title, seo_description.
  'image'       — ProductImage alt_text.
  'options'     — All VariantOptions and VariantOptionValues for a product in a single
                  matrix (one job per (product, language)), keyed by stable PKs (ADR-014 §Q4).
  'slug_only'   — Regenerate the translated slug only; leaves translated text fields unchanged.
  'static_page' — StaticPage title, body, seo_title, seo_description (ADR-018 D1 §4).
                  Writes pages.models.StaticPageTranslation via a deferred import — the
                  'pages' app depends on 'catalog' (TranslationStatus, translation_fingerprint),
                  never the reverse, so this stays a one-directional dependency.

Deprecated sub-types (kept for fallback single-item re-translate only):
  'variant_option'       — VariantOption name (single item).
  'variant_option_value' — VariantOptionValue value label (single item).

Public API for the management command (Phase 4):
  build_prompt(job)                      -> str
  persist_output_from_text(job, raw_text) -> None
    Parses raw_text as JSON, validates, and creates/updates the translation record.
    Raises TranslationPersistError on ERROR-severity validation failure.
    Does NOT set manually_edited_at — that is only for human admin saves (ADR-014 §4).
    Honors manually_edited_at protection: skips update and marks job SKIPPED when
    the existing record has manually_edited_at set, unless overwrite_manual=True is in
    job.input_payload (ADR-014 §Q5).
"""

import json
import logging
import re

from aijobs.registry import JobTypeDefinition, register_job_type

logger = logging.getLogger(__name__)

TRANSLATION_JOB_TYPE = "translation"

# ---------------------------------------------------------------------------
# Error type
# ---------------------------------------------------------------------------

class TranslationPersistError(Exception):
    """
    Raised by persist_output_from_text() when one or more ERROR-severity
    validation checks fail.  The message contains the list of error strings.
    The management command catches this, records the error on the job, and
    increments the retry counter.
    """


# ---------------------------------------------------------------------------
# Prompt builder templates
# ---------------------------------------------------------------------------

_PRODUCT_PROMPT_TEMPLATE = """\
Translate the following {source_lang} product content into {target_lang} for the \
store "{store_name}".

Output a JSON object with exactly these keys:
  title        — translated product title
  description  — translated product description (HTML is allowed if the source uses it)
  seo_title    — translated SEO title ({seo_title_limit}-character limit)
  seo_description — translated SEO description ({seo_description_limit}-character limit)

Length constraints:
  Keep 'seo_title' under {seo_title_limit} characters.
  Keep 'seo_description' under {seo_description_limit} characters.

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_COLLECTION_PROMPT_TEMPLATE = """\
Translate the following {source_lang} collection content into {target_lang} for the \
store "{store_name}".

Output a JSON object with exactly these keys:
  title        — translated collection title
  description  — translated collection description (HTML is allowed if the source uses it)
  seo_title    — translated SEO title ({seo_title_limit}-character limit)
  seo_description — translated SEO description ({seo_description_limit}-character limit)

Length constraints:
  Keep 'seo_title' under {seo_title_limit} characters.
  Keep 'seo_description' under {seo_description_limit} characters.

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_STATIC_PAGE_PROMPT_TEMPLATE = """\
Translate the following {source_lang} static page content into {target_lang} for the \
store "{store_name}".

Output a JSON object with exactly these keys:
  title        — translated page title
  body         — translated page body (HTML is allowed if the source uses it)
  seo_title    — translated SEO title ({seo_title_limit}-character limit)
  seo_description — translated SEO description ({seo_description_limit}-character limit)

Length constraints:
  Keep 'seo_title' under {seo_title_limit} characters.
  Keep 'seo_description' under {seo_description_limit} characters.

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_IMAGE_PROMPT_TEMPLATE = """\
Translate the following {source_lang} product image alt text into {target_lang} for \
the store "{store_name}".

Output a JSON object with exactly one key:
  alt_text — translated image alt text (concise, descriptive, 300-character limit)

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_OPTIONS_PROMPT_TEMPLATE = """\
Translate the following {source_lang} product option names and values into {target_lang} for \
the store "{store_name}".

Output a JSON object with exactly these keys (use the exact key strings):
{field_list}

Translate option names (e.g. "Color", "Size") and their values (e.g. "Red", "Large") \
naturally for {target_lang}.

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_SLUG_ONLY_PROMPT_TEMPLATE = """\
Generate a URL-safe slug for the following {target_lang} content for the store "{store_name}".

Output a JSON object with exactly one key:
  slug — a URL-safe slug (lowercase ASCII letters, digits, and hyphens only; max 255 characters)

The slug must be derived from the translated title.

Content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_VARIANT_OPTION_PROMPT_TEMPLATE = """\
Translate the following {source_lang} product variant option name into {target_lang} \
for the store "{store_name}".

Output a JSON object with exactly one key:
  name — translated option name (e.g. "Color", "Size")

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""

_VARIANT_OPTION_VALUE_PROMPT_TEMPLATE = """\
Translate the following {source_lang} product variant option value into {target_lang} \
for the store "{store_name}".

Output a JSON object with exactly one key:
  value — translated option value label (e.g. "Red", "Large")

Source content:
{fields_block}

Return only the JSON object. No extra text, no markdown fences.
"""


def _fields_block(fields):
    """Format a dict of field values as a readable key: value block."""
    lines = []
    for key, val in fields.items():
        lines.append(f"  {key}: {val!r}")
    return "\n".join(lines)


def build_prompt(job):
    """
    Build the AI prompt string for a translation job.

    Reads source text from job.input_payload. Common keys:
      sub_type    — 'product', 'collection', 'image', 'options', 'slug_only',
                    'variant_option', 'variant_option_value', or 'static_page'
      source_lang — source language code (ISO 639-1)
      target_lang — target language code (ISO 639-1)
      store_name  — human-readable store name

    For 'product'/'collection'/'image'/'variant_option'/'variant_option_value':
      fields      — dict of field_id -> source text
      length_limits (optional) — dict of field_id -> max_chars

    For 'options':
      options     — list of {"option_pk": N, "name": "...", "values": [...]}

    For 'slug_only':
      fields      — dict with at least "title" key (the already-translated title)

    Returns the prompt string.
    Raises ValueError when the payload is malformed or sub_type is unknown.
    """
    payload = job.input_payload or {}
    sub_type = payload.get("sub_type", "")
    source_lang = payload.get("source_lang", "en")
    target_lang = payload.get("target_lang", "")
    store_name = payload.get("store_name", "")
    length_limits = payload.get("length_limits", {})

    if not target_lang:
        raise ValueError(f"Job #{job.pk}: input_payload is missing 'target_lang'.")

    if sub_type == "options":
        return _build_options_prompt(job, payload, source_lang, target_lang, store_name)

    if sub_type == "slug_only":
        fields = payload.get("fields", {})
        if not fields:
            raise ValueError(f"Job #{job.pk}: input_payload is missing 'fields'.")
        return _SLUG_ONLY_PROMPT_TEMPLATE.format(
            target_lang=target_lang,
            store_name=store_name,
            fields_block=_fields_block(fields),
        )

    # All other sub-types require 'fields'.
    fields = payload.get("fields", {})
    if not fields:
        raise ValueError(f"Job #{job.pk}: input_payload is missing 'fields'.")
    fb = _fields_block(fields)

    if sub_type == "product":
        return _PRODUCT_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
            seo_title_limit=length_limits.get("seo_title", 60),
            seo_description_limit=length_limits.get("seo_description", 160),
        )

    if sub_type == "collection":
        return _COLLECTION_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
            seo_title_limit=length_limits.get("seo_title", 60),
            seo_description_limit=length_limits.get("seo_description", 160),
        )

    if sub_type == "static_page":
        return _STATIC_PAGE_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
            seo_title_limit=length_limits.get("seo_title", 60),
            seo_description_limit=length_limits.get("seo_description", 160),
        )

    if sub_type == "image":
        return _IMAGE_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
        )

    if sub_type == "variant_option":
        return _VARIANT_OPTION_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
        )

    if sub_type == "variant_option_value":
        return _VARIANT_OPTION_VALUE_PROMPT_TEMPLATE.format(
            source_lang=source_lang,
            target_lang=target_lang,
            store_name=store_name,
            fields_block=fb,
        )

    raise ValueError(
        f"Job #{job.pk}: unknown sub_type {sub_type!r} in input_payload. "
        "Expected one of: product, collection, image, options, slug_only, "
        "variant_option, variant_option_value, static_page."
    )


def _build_options_prompt(job, payload, source_lang, target_lang, store_name):
    """Build a prompt for the 'options' sub-type (ADR-014 §Q4)."""
    options = payload.get("options")
    if not options:
        raise ValueError(
            f"Job #{job.pk}: input_payload is missing 'options' list for sub_type='options'."
        )

    # Build the list of expected output keys and source content block.
    key_lines = []
    source_lines = []
    for opt in options:
        opt_pk = opt.get("option_pk")
        opt_name = opt.get("name", "")
        key_lines.append(f"  option_{opt_pk}_name — translated name for option '{opt_name}'")
        source_lines.append(f"  option_{opt_pk}_name (option): {opt_name!r}")
        for val in opt.get("values", []):
            val_pk = val.get("value_pk")
            val_label = val.get("value", "")
            key_lines.append(f"  value_{val_pk}_value — translated value for '{val_label}'")
            source_lines.append(f"  value_{val_pk}_value: {val_label!r}")

    if not key_lines:
        raise ValueError(
            f"Job #{job.pk}: 'options' list is empty — nothing to translate."
        )

    return _OPTIONS_PROMPT_TEMPLATE.format(
        source_lang=source_lang,
        target_lang=target_lang,
        store_name=store_name,
        field_list="\n".join(key_lines),
        fields_block="\n".join(source_lines),
    )


# ---------------------------------------------------------------------------
# JSON extraction from raw AI output
# ---------------------------------------------------------------------------

_MARKDOWN_JSON_RE = re.compile(r"```(?:json)?\s*([\s\S]*?)```", re.IGNORECASE)


def _extract_json(raw_text):
    """
    Parse raw AI output as JSON.

    Handles two formats:
      1. Bare JSON object/array.
      2. JSON wrapped in a markdown code fence (```json ... ```).

    Returns the parsed dict (or raises json.JSONDecodeError / ValueError).
    """
    text = raw_text.strip()
    match = _MARKDOWN_JSON_RE.search(text)
    if match:
        text = match.group(1).strip()
    return json.loads(text)


# ---------------------------------------------------------------------------
# Persist output from raw text — called by the management command
# ---------------------------------------------------------------------------

def persist_output_from_text(job, raw_text):
    """
    Parse raw AI output, validate it, and create/update the translation record.

    Manual-override guard (ADR-014 §Q5, MCP-052):
      If the target translation record already exists with manually_edited_at set,
      skip the update and mark the job SKIPPED, unless job.input_payload contains
      "overwrite_manual": true.  When overwriting, also resets manually_edited_at=NULL
      (the record becomes AI content again).

    Validation logic:
      ERROR   — non-empty title missing for Product/Collection (blocks persist)
              — required field missing for the sub_type
      WARNING — seo_title > 60 chars; seo_description > 160 chars (recorded only)

    Raises TranslationPersistError when any ERROR-severity check fails.
    On success, creates/updates the relevant translation model with status=PUBLISHED,
    ai_job=job, and source_fingerprint computed from the source-field payload.
    Does NOT set manually_edited_at (ADR-014 §4).

    Target-model dispatch:
      'catalog.Product'            -> ProductTranslation  (sub_type 'product', 'slug_only')
      'catalog.Collection'         -> CollectionTranslation  (sub_type 'collection')
      'catalog.ProductImage'       -> ProductImageTranslation  (sub_type 'image')
      'catalog.Product'            -> VariantOption/ValueTranslation  (sub_type 'options')
      'pages.StaticPage'           -> pages.models.StaticPageTranslation  (sub_type 'static_page')
    """
    payload = job.input_payload or {}
    sub_type = payload.get("sub_type", "")
    lang_code = job.lang

    # --- Parse ---
    try:
        data = _extract_json(raw_text)
    except (json.JSONDecodeError, ValueError) as exc:
        raise TranslationPersistError(
            f"Job #{job.pk}: failed to parse AI output as JSON: {exc}"
        ) from exc

    if not isinstance(data, dict):
        raise TranslationPersistError(
            f"Job #{job.pk}: AI output is not a JSON object (got {type(data).__name__})."
        )

    # --- Route to the correct persist function ---
    if sub_type == "options":
        return _persist_options(job, data, lang_code, payload)

    if sub_type == "slug_only":
        return _persist_slug_only(job, data, lang_code, payload)

    # --- Validate for single-record sub-types ---
    errors = []
    warnings = []
    length_limits = payload.get("length_limits", {})

    if sub_type in ("product", "collection", "static_page"):
        title = data.get("title", "").strip()
        if not title:
            errors.append("'title' must be non-empty.")
        seo_title = data.get("seo_title", "")
        seo_desc = data.get("seo_description", "")
        seo_title_limit = length_limits.get("seo_title", 60)
        seo_desc_limit = length_limits.get("seo_description", 160)
        if len(seo_title) > seo_title_limit:
            warnings.append(
                f"'seo_title' is {len(seo_title)} chars (limit {seo_title_limit})."
            )
        if len(seo_desc) > seo_desc_limit:
            warnings.append(
                f"'seo_description' is {len(seo_desc)} chars (limit {seo_desc_limit})."
            )

    elif sub_type == "image":
        if not data.get("alt_text", "").strip():
            # Images may legitimately have no alt text (decorative) — WARNING only
            warnings.append("'alt_text' is empty; decorative image assumed.")

    elif sub_type == "variant_option":
        if not data.get("name", "").strip():
            errors.append("'name' must be non-empty for variant_option translations.")

    elif sub_type == "variant_option_value":
        if not data.get("value", "").strip():
            errors.append("'value' must be non-empty for variant_option_value translations.")

    else:
        errors.append(f"Unknown sub_type {sub_type!r} — cannot validate output.")

    for warning in warnings:
        logger.warning("Job #%s: validation warning — %s", job.pk, warning)

    if errors:
        raise TranslationPersistError(
            f"Job #{job.pk}: validation errors — {'; '.join(errors)}"
        )

    # --- Persist ---
    from catalog.models import (
        CollectionTranslation,
        ProductImageTranslation,
        ProductTranslation,
        TranslationStatus,
        VariantOptionTranslation,
        VariantOptionValueTranslation,
        translation_fingerprint,
    )
    from aijobs.models import AiJobStatus

    published = TranslationStatus.PUBLISHED
    overwrite_manual = payload.get("overwrite_manual", False)

    if sub_type in ("product", "collection"):
        model_cls = ProductTranslation if sub_type == "product" else CollectionTranslation
        parent_fk = "product_id" if sub_type == "product" else "collection_id"
        parent_id = job.target_id
        lookup = {
            "store": job.store,
            parent_fk: parent_id,
            "lang_code": lang_code,
        }

        # Manual-override guard (ADR-014 §Q5)
        existing = model_cls.objects.cross_store_unsafe().filter(**lookup).first()
        if existing and existing.manually_edited_at and not overwrite_manual:
            logger.warning(
                "Job #%s: skipping AI overwrite of manually-edited %s pk=%s lang=%s.",
                job.pk, model_cls.__name__, existing.pk, lang_code,
            )
            from aijobs.models import AiJob
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.SKIPPED,
            )
            return

        # Compute source fingerprint from the payload's source fields.
        source_fields = payload.get("fields", {})
        fp = translation_fingerprint(
            source_fields.get("title", ""),
            source_fields.get("description", ""),
            source_fields.get("seo_title", ""),
            source_fields.get("seo_description", ""),
        )

        defaults = {
            "title": data.get("title", "").strip(),
            "description": data.get("description", ""),
            "seo_title": data.get("seo_title", ""),
            "seo_description": data.get("seo_description", ""),
            "status": published,
            "ai_job": job,
            "source_fingerprint": fp,
        }
        # When overwriting a manually-edited record, reset the manual flag.
        if existing and existing.manually_edited_at and overwrite_manual:
            defaults["manually_edited_at"] = None

        model_cls.objects.cross_store_unsafe().update_or_create(
            **lookup,
            defaults=defaults,
        )

    elif sub_type == "static_page":
        # Deferred import: 'pages' depends on 'catalog', never the reverse.
        from pages.models import StaticPageTranslation

        lookup = {"store": job.store, "page_id": job.target_id, "lang_code": lang_code}

        existing = StaticPageTranslation.objects.cross_store_unsafe().filter(**lookup).first()
        if existing and existing.manually_edited_at and not overwrite_manual:
            logger.warning(
                "Job #%s: skipping AI overwrite of manually-edited StaticPageTranslation pk=%s lang=%s.",
                job.pk, existing.pk, lang_code,
            )
            from aijobs.models import AiJob
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.SKIPPED,
            )
            return

        # Fingerprint field order (title, body, seo_title, seo_description) — must
        # match pages.models.static_page_fingerprint / pages.signals exactly.
        source_fields = payload.get("fields", {})
        fp = translation_fingerprint(
            source_fields.get("title", ""),
            source_fields.get("body", ""),
            source_fields.get("seo_title", ""),
            source_fields.get("seo_description", ""),
        )

        defaults = {
            "title": data.get("title", "").strip(),
            "body": data.get("body", ""),
            "seo_title": data.get("seo_title", ""),
            "seo_description": data.get("seo_description", ""),
            "status": published,
            "ai_job": job,
            "source_fingerprint": fp,
        }
        if existing and existing.manually_edited_at and overwrite_manual:
            defaults["manually_edited_at"] = None

        StaticPageTranslation.objects.cross_store_unsafe().update_or_create(
            **lookup,
            defaults=defaults,
        )

    elif sub_type == "image":
        image_id = job.target_id
        lookup = {"store": job.store, "image_id": image_id, "lang_code": lang_code}

        existing = ProductImageTranslation.objects.cross_store_unsafe().filter(**lookup).first()
        if existing and existing.manually_edited_at and not overwrite_manual:
            logger.warning(
                "Job #%s: skipping AI overwrite of manually-edited ProductImageTranslation pk=%s.",
                job.pk, existing.pk,
            )
            from aijobs.models import AiJob
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.SKIPPED,
            )
            return

        source_fields = payload.get("fields", {})
        fp = translation_fingerprint(source_fields.get("alt_text", ""))
        defaults = {
            "alt_text": data.get("alt_text", ""),
            "status": published,
            "ai_job": job,
            "source_fingerprint": fp,
        }
        if existing and existing.manually_edited_at and overwrite_manual:
            defaults["manually_edited_at"] = None
        ProductImageTranslation.objects.cross_store_unsafe().update_or_create(
            **lookup, defaults=defaults,
        )

    elif sub_type == "variant_option":
        option_id = job.target_id
        lookup = {"store": job.store, "option_id": option_id, "lang_code": lang_code}

        existing = VariantOptionTranslation.objects.cross_store_unsafe().filter(**lookup).first()
        if existing and existing.manually_edited_at and not overwrite_manual:
            logger.warning(
                "Job #%s: skipping AI overwrite of manually-edited VariantOptionTranslation pk=%s.",
                job.pk, existing.pk,
            )
            from aijobs.models import AiJob
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.SKIPPED,
            )
            return

        source_fields = payload.get("fields", {})
        fp = translation_fingerprint(source_fields.get("name", ""))
        defaults = {
            "name": data.get("name", "").strip(),
            "status": published,
            "ai_job": job,
            "source_fingerprint": fp,
        }
        if existing and existing.manually_edited_at and overwrite_manual:
            defaults["manually_edited_at"] = None
        VariantOptionTranslation.objects.cross_store_unsafe().update_or_create(
            **lookup, defaults=defaults,
        )

    elif sub_type == "variant_option_value":
        ov_id = job.target_id
        lookup = {"store": job.store, "option_value_id": ov_id, "lang_code": lang_code}

        existing = VariantOptionValueTranslation.objects.cross_store_unsafe().filter(**lookup).first()
        if existing and existing.manually_edited_at and not overwrite_manual:
            logger.warning(
                "Job #%s: skipping AI overwrite of manually-edited VariantOptionValueTranslation pk=%s.",
                job.pk, existing.pk,
            )
            from aijobs.models import AiJob
            AiJob.objects.cross_store_unsafe().filter(pk=job.pk).update(
                status=AiJobStatus.SKIPPED,
            )
            return

        source_fields = payload.get("fields", {})
        fp = translation_fingerprint(source_fields.get("value", ""))
        defaults = {
            "value": data.get("value", "").strip(),
            "status": published,
            "ai_job": job,
            "source_fingerprint": fp,
        }
        if existing and existing.manually_edited_at and overwrite_manual:
            defaults["manually_edited_at"] = None
        VariantOptionValueTranslation.objects.cross_store_unsafe().update_or_create(
            **lookup, defaults=defaults,
        )


def _persist_options(job, data, lang_code, payload):
    """
    Persist an 'options' matrix translation: upserts VariantOptionTranslation and
    VariantOptionValueTranslation rows for all options/values of a product in one
    transaction (ADR-014 §Q4, §I all-or-nothing).

    Output field ids must follow the pattern option_{pk}_name / value_{pk}_value.
    Each PK is verified to belong to the target product and store before persisting (§XV-5).
    """
    from django.db import transaction
    from catalog.models import (
        TranslationStatus,
        VariantOption,
        VariantOptionTranslation,
        VariantOptionValue,
        VariantOptionValueTranslation,
        translation_fingerprint,
    )
    from aijobs.models import AiJobStatus

    published = TranslationStatus.PUBLISHED
    product_id = job.target_id
    overwrite_manual = payload.get("overwrite_manual", False)

    # Pre-validate: parse PKs from the output keys.
    option_updates = {}  # option_pk -> translated name
    value_updates = {}   # value_pk -> translated value

    for key, val in data.items():
        if key.startswith("option_") and key.endswith("_name"):
            try:
                pk = int(key[len("option_"):-len("_name")])
            except ValueError:
                raise TranslationPersistError(
                    f"Job #{job.pk}: malformed option key {key!r} in AI output."
                )
            if not str(val).strip():
                raise TranslationPersistError(
                    f"Job #{job.pk}: translated name for key {key!r} is empty."
                )
            option_updates[pk] = str(val).strip()

        elif key.startswith("value_") and key.endswith("_value"):
            try:
                pk = int(key[len("value_"):-len("_value")])
            except ValueError:
                raise TranslationPersistError(
                    f"Job #{job.pk}: malformed value key {key!r} in AI output."
                )
            if not str(val).strip():
                raise TranslationPersistError(
                    f"Job #{job.pk}: translated value for key {key!r} is empty."
                )
            value_updates[pk] = str(val).strip()

    if not option_updates and not value_updates:
        raise TranslationPersistError(
            f"Job #{job.pk}: AI output contains no option_*/value_* keys for 'options' sub-type."
        )

    # Verify ownership: each PK must belong to the target product AND store.
    if option_updates:
        valid_option_pks = set(
            VariantOption.objects.cross_store_unsafe()
            .filter(pk__in=option_updates, product_id=product_id, store=job.store)
            .values_list("pk", flat=True)
        )
        bad_pks = set(option_updates) - valid_option_pks
        if bad_pks:
            raise TranslationPersistError(
                f"Job #{job.pk}: option PKs {bad_pks} do not belong to "
                f"product {product_id} / store {job.store_id}."
            )

    if value_updates:
        valid_value_pks = set(
            VariantOptionValue.objects.cross_store_unsafe()
            .filter(pk__in=value_updates, option__product_id=product_id, store=job.store)
            .values_list("pk", flat=True)
        )
        bad_pks = set(value_updates) - valid_value_pks
        if bad_pks:
            raise TranslationPersistError(
                f"Job #{job.pk}: value PKs {bad_pks} do not belong to "
                f"product {product_id} / store {job.store_id}."
            )

    # Persist all-or-nothing (§I).
    with transaction.atomic():
        for opt_pk, translated_name in option_updates.items():
            opt_lookup = {"store": job.store, "option_id": opt_pk, "lang_code": lang_code}
            existing = VariantOptionTranslation.objects.cross_store_unsafe().filter(**opt_lookup).first()
            if existing and existing.manually_edited_at and not overwrite_manual:
                logger.warning(
                    "Job #%s: skipping manually-edited VariantOptionTranslation pk=%s.",
                    job.pk, existing.pk,
                )
                continue
            source_name = ""
            try:
                source_name = VariantOption.objects.cross_store_unsafe().get(pk=opt_pk).name
            except VariantOption.DoesNotExist:
                pass
            defaults = {
                "name": translated_name,
                "status": published,
                "ai_job": job,
                "source_fingerprint": translation_fingerprint(source_name),
            }
            if existing and existing.manually_edited_at and overwrite_manual:
                defaults["manually_edited_at"] = None
            VariantOptionTranslation.objects.cross_store_unsafe().update_or_create(
                **opt_lookup, defaults=defaults,
            )

        for val_pk, translated_value in value_updates.items():
            val_lookup = {"store": job.store, "option_value_id": val_pk, "lang_code": lang_code}
            existing = VariantOptionValueTranslation.objects.cross_store_unsafe().filter(**val_lookup).first()
            if existing and existing.manually_edited_at and not overwrite_manual:
                logger.warning(
                    "Job #%s: skipping manually-edited VariantOptionValueTranslation pk=%s.",
                    job.pk, existing.pk,
                )
                continue
            source_value = ""
            try:
                source_value = VariantOptionValue.objects.cross_store_unsafe().get(pk=val_pk).value
            except VariantOptionValue.DoesNotExist:
                pass
            defaults = {
                "value": translated_value,
                "status": published,
                "ai_job": job,
                "source_fingerprint": translation_fingerprint(source_value),
            }
            if existing and existing.manually_edited_at and overwrite_manual:
                defaults["manually_edited_at"] = None
            VariantOptionValueTranslation.objects.cross_store_unsafe().update_or_create(
                **val_lookup, defaults=defaults,
            )


def _persist_slug_only(job, data, lang_code, payload):
    """
    Persist a 'slug_only' translation: re-saves the existing published translation
    with _slug_hint set, causing the permalink signal to update the auto-created
    permalink slug (ADR-014 §Q6).

    Does NOT modify title, description, seo_title, or seo_description.
    If no published translation exists for (store, parent, lang_code), raises
    TranslationPersistError.
    """
    from catalog.models import (
        ProductTranslation,
        CollectionTranslation,
        TranslationStatus,
    )

    slug = data.get("slug", "").strip()
    if not slug:
        raise TranslationPersistError(
            f"Job #{job.pk}: 'slug' must be non-empty for slug_only sub-type."
        )

    published = TranslationStatus.PUBLISHED
    target_model = job.target_model

    if target_model == "catalog.Product":
        model_cls = ProductTranslation
        lookup = {"store": job.store, "product_id": job.target_id, "lang_code": lang_code}
    elif target_model == "catalog.Collection":
        model_cls = CollectionTranslation
        lookup = {"store": job.store, "collection_id": job.target_id, "lang_code": lang_code}
    else:
        raise TranslationPersistError(
            f"Job #{job.pk}: slug_only sub-type not supported for target_model {target_model!r}."
        )

    try:
        tr = model_cls.objects.cross_store_unsafe().get(**lookup)
    except model_cls.DoesNotExist:
        raise TranslationPersistError(
            f"Job #{job.pk}: no existing {model_cls.__name__} for {lookup} — "
            "cannot apply slug_only update without a published record."
        )

    if tr.status != published:
        raise TranslationPersistError(
            f"Job #{job.pk}: {model_cls.__name__} pk={tr.pk} is not PUBLISHED — "
            "cannot apply slug_only update to a draft record."
        )

    # Persist slug_hint to DB so the permalink signal (and admin) can read it.
    # update_fields is minimal — title/description/seo_* are unchanged.
    # The signal reads instance.slug_hint during the save() call.
    tr.slug_hint = slug
    tr.save(update_fields=["slug_hint", "updated_at"])


# ---------------------------------------------------------------------------
# Registry callbacks (ADR-003 §7)
# ---------------------------------------------------------------------------

def _build_prompt(job):
    """Registry callback — delegates to the public build_prompt()."""
    return build_prompt(job)


def _validate_output(raw_output, job):
    """
    Registry validate hook — returns error strings or empty list.

    Returns errors as strings rather than raising so that the registry caller
    can record them as AiJobValidation rows. persist_output_from_text() does
    its own validation when called by the management command.
    """
    try:
        data = _extract_json(raw_output)
    except (json.JSONDecodeError, ValueError) as exc:
        return [f"JSON parse error: {exc}"]
    if not isinstance(data, dict):
        return [f"Output is not a JSON object (got {type(data).__name__})."]
    payload = job.input_payload or {}
    sub_type = payload.get("sub_type", "")
    errors = []
    if sub_type in ("product", "collection", "static_page"):
        if not data.get("title", "").strip():
            errors.append("'title' must be non-empty.")
    elif sub_type == "variant_option":
        if not data.get("name", "").strip():
            errors.append("'name' must be non-empty.")
    elif sub_type == "variant_option_value":
        if not data.get("value", "").strip():
            errors.append("'value' must be non-empty.")
    elif sub_type == "slug_only":
        if not data.get("slug", "").strip():
            errors.append("'slug' must be non-empty.")
    return errors


def _persist_output(job_run, outputs):
    """
    Registry persist callback — called by the registry-based scheduler flow.

    Reconstructs raw_text from the AiJobOutput 'raw_response' field (if present)
    and delegates to persist_output_from_text().  For Phase 4 (management command),
    persist_output_from_text() is called directly.
    """
    # outputs is a list of AiJobOutput; we expect one row with field_id='raw_response'
    # or reconstruct JSON from individual field rows.
    raw_text = None
    for output in outputs:
        if output.field_id == "raw_response":
            raw_text = output.content
            break
    if raw_text is None:
        # Reconstruct a JSON object from individual field rows.
        data = {o.field_id: o.content for o in outputs}
        raw_text = json.dumps(data)
    try:
        persist_output_from_text(job_run.job, raw_text)
    except TranslationPersistError as exc:
        raise RuntimeError(str(exc)) from exc


def _estimate_tokens(job):
    """Conservative default estimate for a product/collection translation job."""
    payload = job.input_payload or {}
    sub_type = payload.get("sub_type", "")

    if sub_type == "options":
        # Estimate from the options list.
        options = payload.get("options", [])
        total_chars = sum(
            len(opt.get("name", "")) + sum(len(v.get("value", "")) for v in opt.get("values", []))
            for opt in options
        )
    else:
        fields = payload.get("fields", {})
        total_chars = sum(len(v) for v in fields.values() if isinstance(v, str))

    # Rough estimate: 1 token ≈ 4 chars; add 300 tokens for the prompt template.
    return max(300 + total_chars // 4, 300)


register_job_type(
    JobTypeDefinition(
        job_type=TRANSLATION_JOB_TYPE,
        display_name="Content Translation",
        build_prompt=_build_prompt,
        validate_output=_validate_output,
        persist_output=_persist_output,
        estimate_tokens=_estimate_tokens,
        capabilities=["text"],
        # Translations are highest priority per ADR-003 §1.
        default_priority=100,
    )
)
