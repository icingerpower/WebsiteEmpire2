# ADR-030: Security badge designer (trust badges on product/cart/checkout)

**Status:** ACCEPTED (human, 2026-07-11) — the truthful-only built-in badge set
(§1, D1) is approved as designed, recorded as a deliberate product-integrity
decision to deviate from the CommerceHQ reference screenshots; the D5 live
re-render preview pane stays out-of-scope, PENDING a follow-up ticket.
Implements `TICKET-046` (Phase 3, P3, complexity **S**). Depends on
`TICKET-029` (ADR-012 storefront theme system — the `slot.security_badge`
slot is already declared and already rendered on two of the three target
pages).

**Extends (frozen — not re-opened here):**
- ADR-012 D9 — the `SlotProvider` registry (`storefront/slots.py`). This ADR
  adds one provider, no changes to the registry contract itself.
- ADR-015 §8 — checkout already calls `{% render_slot "security_badge" %}`
  under the Pay button (`storefront/templates/storefront/pages/checkout.html`,
  CK-010). `product.html` already calls the same slot under the buy button.
  **Only `cart.html` is missing the call** — a one-line addition (§4).
- ADR-006 §7 / ADR-006-R §1.6 — `payments.models.PaymentMethod.method_family`
  and `ProcessorAccount` are the source of truth this ADR reads from to
  decide which payment-network logos are truthful to show (§3).
- `media/validators.py::validate_image_file` — reused unmodified for the
  optional custom-image upload (§2).
- `05_database_schema.md` §12 sketch — `SecurityBadge [S]` (`badge_set_json`,
  `heading_text`, `heading_color`, `border_style`, `locations_json`). This
  ADR is the concrete design replacing that sketch; **marked DECIDED — ADR-030
  with the field list in §2 (2026-07-11)** — see
  `specs/ecommerce_engine/05_database_schema.md` §12.

**Binding constraints (design-pattern-ideas / organization policy):**
- §XV-1 (invisible failures are the enemy) — an unrecognized `preset_key`,
  an unrecognized `location`, or a store-uploaded SVG must fail loudly at
  `clean()`/upload time, never silently render nothing or render something
  unintended.
- Registry, not hardcoding — badge presets and icon partials are a **code
  registry** (`badges/badge_presets.py`), the exact shape of
  `engagement/themes.py::OVERLAY_THEMES` (ADR-027 D10): adding a preset is a
  template + a preview SVG + one registry entry, no migration.
- **Product-integrity constraint (this ADR's central decision, §1):** a
  security/trust badge is a factual claim to the shopper. The platform must
  never ship a built-in badge asset that asserts a certification, monitoring
  service, or encryption standard the store does not actually have. This is
  the reason this design deviates from the reference screenshots (§1).

---

## 1. D1 — What "badge designer" means here, and why it is not a copy of the reference screenshots

**Context.** The source screenshots (`super-admin-06-security-badge.jpg`,
`super-admin-06-security-badge-02-edit.jpg`) show a competitor product
(CommerceHQ) whose "BADGE COMBINATION" gallery is four **pre-baked raster
images**, each compositing real third-party marks: Visa/Mastercard/Amex/
Discover/PayPal logos, a "Powered by Stripe · SECURED SITE" lock graphic, and
— critically — two decorative seals with no real backing: **"SSL ENCRYPTION
· 24-HOUR SURVEILLANCE"** and **"AES-256 BIT"**. Nothing in that product
verifies that the merchant's site is actually monitored 24 hours, actually
uses AES-256, or actually accepts every logo shown. A store could select the
"AES-256 BIT" seal while running plaintext HTTP, or show a PayPal logo while
having no PayPal account configured at all.

**Decision.** Build a **curated, platform-authored badge system** instead of
copying the combination images:

1. Every **graphical trust mark** the platform ships is either (a) a
   generic, self-descriptive glyph the platform itself can make true by
   construction (a lock/shield icon, an "SSL Secured" seal gated on
   `request.is_secure()` — §3), or (b) a real payment-network logo
   **derived from the store's actually-enabled `PaymentMethod` rows**, never
   picked from a fixed list independent of configuration (§3).
2. **No third-party certification marks** (McAfee Secured, Norton Secured,
   TRUSTe, BBB, "24-Hour Surveillance", "AES-256 BIT") ship as built-in
   assets — the platform has no way to verify any store's enrollment in
   such a service, and shipping the mark anyway is a false claim of
   certification (FTC-style dark-pattern risk, and a brand/trademark risk
   for using a certifying body's mark without their license).
3. Heading **text** is store-authored free copy (like any other on-site
   copy — product descriptions, ad text) and is the store's own
   responsibility to keep truthful; only the **images** are
   platform-controlled and must be truthful by construction. This is the
   line drawn between "content the merchant is accountable for" and
   "assets the platform is accountable for."
4. A store that genuinely holds a real third-party certification (their own
   McAfee/Norton contract, their own security audit badge, etc.) can still
   display it via **"Upload your own image"** (§2) — that is the store's
   own asset and the store's own attestation, exactly like a store logo
   upload. The platform is not asserting anything about an uploaded image;
   it is hosting it.

**Options considered:**

| Option | Tradeoff |
|---|---|
| Copy the 4 raster combination images verbatim | Fastest to build, matches the screenshot pixel-for-pixel — but ships two badges asserting security services no store has, and hardcodes 5 specific trademarked logos regardless of what the store actually accepts. Rejected: violates the truthful-by-construction constraint and is not reusable across the platform's actual (currently 2, Stripe/PayPal) processor set. |
| Curated built-in set + derived payment logos + optional custom upload (chosen) | Slightly more design work (a small registry + one derivation query) but every built-in asset is either self-verifying or config-derived; stores wanting more can upload their own and own that claim. Matches the registry pattern already used for overlay themes (ADR-027 D10). |
| Store-uploaded images only, no built-in set | Zero platform liability, but fails the ticket's "preset combinations" requirement and leaves a new store with an empty badge picker on day one (worse first-run experience than every other admin screen in this engine, which all ship sensible defaults). Rejected. |

**Why:** the ticket is complexity **S** — the fix is not to add process, it
is to swap **which four raster files exist** for **one small registry of
truthful compositions**. This is not over-engineering: it is the same
registry-of-presets shape already shipped for overlay themes, applied to a
narrower asset (badges instead of layouts).

---

## 2. D2 — Model

One model, `badges.SecurityBadge(StoreOwnedModel)` — a plain per-store table
(the screenshot's list view shows multiple named badges with independent
active state, not a singleton).

```python
class TrustBadgeLocation(models.TextChoices):
    PRODUCT = "product", "Product page"
    CART = "cart", "Cart"
    CHECKOUT = "checkout", "Checkout"


class SecurityBadge(StoreOwnedModel):
    name = models.CharField(max_length=100)  # admin label only, e.g. "Safe checkout"
    is_active = models.BooleanField(default=True)

    # --- Badge combination (§1, §3) ---
    preset_key = models.CharField(max_length=40)  # key into badges.badge_presets.BADGE_PRESETS,
                                                   # or the literal "custom" (see clean()).
    custom_image = models.ImageField(
        upload_to="security_badges/%Y/%m/",
        blank=True, null=True,
        validators=[validate_image_file],
    )

    # --- Refine style ---
    heading_enabled = models.BooleanField(default=True)
    # Ordered, per-segment coloring — generalizes CommerceHQ's 3 hardcoded
    # English words ("Guaranteed"/"SAFE"/"Checkout") into a translatable,
    # store-authored, arbitrary-length structure. Store's own copy (§1.3).
    heading_segments_json = models.JSONField(default=list)
    # [{"text": "Guaranteed", "color": "#000000"}, {"text": "SAFE", "color": "#088650"}, ...]

    border_enabled = models.BooleanField(default=True)
    border_color = models.CharField(max_length=7, default="#000000")  # hex
    border_width_px = models.PositiveSmallIntegerField(default=5)

    # --- Placement (screenshot "LOCATION" section) ---
    locations_json = models.JSONField(default=dict)
    # {"product": {"enabled": false, "width_px": 200},
    #  "cart":    {"enabled": false, "width_px": 200},
    #  "checkout":{"enabled": true,  "width_px": 200}}

    created_at = models.DateTimeField(auto_now_add=True)
    updated_at = models.DateTimeField(auto_now=True)

    class Meta:
        verbose_name = "security badge"
        verbose_name_plural = "security badges"
        indexes = [models.Index(fields=["store", "is_active"])]

    def clean(self):
        # §XV-1: every one of these is a loud ValidationError, never a
        # silent skip — a broken row must never render nothing in prod
        # having passed admin save.
        if self.preset_key != "custom" and self.preset_key not in BADGE_PRESETS:
            raise ValidationError(f"Unknown preset_key {self.preset_key!r}.")
        if self.preset_key == "custom" and not self.custom_image:
            raise ValidationError("custom_image is required when preset_key='custom'.")
        if self.preset_key != "custom" and self.custom_image:
            raise ValidationError(
                "custom_image is only used when preset_key='custom' — clear the "
                "upload or switch preset_key, do not leave a dangling unused upload."
            )
        _validate_heading_segments(self.heading_segments_json)  # ≤6 segments, hex color, ≤40 chars/segment
        _validate_locations(self.locations_json)  # keys ⊆ TrustBadgeLocation values; width_px in [100, 800]
```

`custom_image` reuses `media.validators.validate_image_file` **plus** a
field-local, narrower content-type allow-list that **excludes
`image/svg+xml`** even if the platform's global `ALLOWED_IMAGE_TYPES` setting
ever includes it elsewhere. An uploaded SVG can carry inline
`<script>`/event-handler payloads; this field renders next to the payment
button on every product/cart/checkout page of the store, so it is treated as
a high-trust rendering zone. Raster only (PNG/JPEG/WebP).

**Options considered:** a JSON blob of arbitrary badge-icon keys (a
build-your-own composer) vs. a small fixed set of named presets (chosen) vs.
per-location separate rows. Named presets chosen: matches the screenshot's
actual UI (pick one of several combination cards), is far less code than a
composer, and keeps the "no unverified icon combination" invariant enforced
by the registry rather than by runtime validation of an open-ended icon list.

---

## 3. D3 — Badge preset registry + truthful payment-logo derivation

**`badges/badge_presets.py`** — a code registry, identical shape to
`engagement/themes.py::OVERLAY_THEMES` (ADR-027 D10): no DB rows, no
migration to add a preset, just a template partial + a preview SVG + one
dict entry.

```python
BADGE_PRESETS = {
    "trust_and_cards": {
        "template": "storefront/partials/security_badges/trust_and_cards.html",
        "name": "Trusted & secure + accepted cards",
        "preview": "badges/previews/trust_and_cards.svg",
    },
    "seal_and_cards": {
        "template": "storefront/partials/security_badges/seal_and_cards.html",
        "name": "SSL seal + accepted cards",
        "preview": "badges/previews/seal_and_cards.svg",
    },
    "minimal_lock": {
        "template": "storefront/partials/security_badges/minimal_lock.html",
        "name": "Minimal lock icon",
        "preview": "badges/previews/minimal_lock.svg",
    },
    "payment_logos_only": {
        "template": "storefront/partials/security_badges/payment_logos_only.html",
        "name": "Accepted payment methods only",
        "preview": "badges/previews/payment_logos_only.svg",
    },
}
```

Every preset template `{% include %}`s a single shared partial,
`storefront/partials/security_badges/_payment_logos.html`, instead of each
preset hardcoding its own logo list. That partial receives one context
variable, `accepted_families: set[str]`, and renders icon partials strictly
from it:

- `"card"` in `accepted_families` → render the bundled generic card-network
  icon set (`visa.svg`, `mastercard.svg`, `amex.svg`, `discover.svg`).
  Bundled as one unit because the platform's only `card`-family connector
  today is Stripe (`payments.models.ProcessorType.STRIPE`), which accepts
  all four networks — showing the bundle is accurate for every store with
  `card` enabled. **PENDING (low):** if a future card processor supports a
  narrower network set, this bundle must become processor-aware; tracked
  as a follow-up, not blocking this S ticket.
- `"paypal"` in `accepted_families` → render `paypal.svg`.
- `"crypto"` in `accepted_families` → no icon in v1 (no crypto processor
  ships yet per `ProcessorType`; add one when TICKET-041 lands).

`accepted_families` is computed once per render as:

```python
accepted_families = set(
    PaymentMethod.objects.filter(is_enabled=True).values_list("method_family", flat=True)
)
```

**Options considered for the derivation source:**

| Option | Tradeoff |
|---|---|
| `PaymentMethod.is_enabled` (platform-level, chosen) | One cheap query (2–3 rows platform-wide), no per-store routing evaluation. Slightly approximate: it does not confirm THIS store's organization currently has a *healthy* `ProcessorAccount` behind that family — only that the family is enabled platform-wide. |
| `payments.routing.preview_route(store, country, currency, total_cents, method_family)` per family | Store-accurate (matches exactly what checkout would show), but it is a **transactional routing evaluation** that needs country/currency/amount — none of which exist for a cosmetic badge shown on a product page to an anonymous visitor. Calling it here would be routing-engine reuse for the wrong purpose (over-engineering an S-ticket cosmetic feature) and adds real query cost to every product/cart page view. Rejected for v1. |

**Why the approximate option is acceptable:** a trust badge is decorative,
not transactional — it is never the system that decides whether a payment
method is offered (checkout's own `preview_route`-gated buttons remain the
only authoritative path, unchanged by this ADR). The worst-case failure mode
of the platform-level check is showing a payment logo for a family that is
enabled platform-wide but momentarily unrouteable for one store's org — a
narrow, rare, cosmetic-only inaccuracy, not the "advertise a certification
you don't have" problem this ADR exists to prevent. Cache the query result
briefly (e.g. Django cache, 5 minute TTL, key `"security_badge:accepted_families"`)
since `PaymentMethod` rows change on the order of "months," not "requests."

**Self-verifying "secure" claims (§1.1):** any icon whose implied claim is
"this connection is encrypted" (`lock_shield.svg`, `ssl_seal.svg`) is gated
in the provider, not by admin configuration:

```python
is_secure = request.is_secure()
# lock_shield / ssl_seal only rendered when is_secure is True.
```

This makes the claim true by construction even for a store whose custom
domain has TLS verification still pending (`TICKET-040`'s DNS/TLS flow) — on
plain HTTP the icon is silently omitted rather than lying, and there is
nothing for an admin to misconfigure.

---

## 4. D4 — Rendering: SlotProvider + explicit (never sniffed) location context

**New app `badges`** (added to `INSTALLED_APPS`), following the exact
self-registration shape of `chat`/`engagement`/`pixels`:

```python
# badges/apps.py
class BadgesConfig(AppConfig):
    name = "badges"
    verbose_name = "Security badges"

    def ready(self):
        from storefront.slots import register
        from badges.slot_provider import SecurityBadgeSlotProvider
        register(SecurityBadgeSlotProvider())
```

`{% render_slot "security_badge" %}` already exists in `product.html`
(line 136) and `checkout.html` (line 281). **`cart.html` is the only
template missing it** — add one line, matching the existing calls exactly
(`ADR-015`/`ADR-012` slot contract, no template restructuring).

**Location signal — explicit, never sniffed.** ADR-022 D5 established the
rule this engine already follows for slot context: a provider must read an
**explicit** context key a view sets on purpose, never infer meaning by
sniffing which unrelated keys happen to be present (that ADR's own
"Corrected design" addendum exists because an inference-based shortcut broke
in a case nobody had modeled). `render_slot()`'s tag signature
(`render_slot(context, slot_name)`) is a frozen, shared contract used by
every slot in the platform — extending it with a new keyword argument for
one provider's benefit is not needed here and is out of scope for an S
ticket. Instead, each of the three views sets one plain context key before
rendering its template — the same mechanism `pixel_events` already uses:

```python
context["trust_badge_location"] = "product"   # storefront/views.py, product branch of page_view
context["trust_badge_location"] = "cart"      # storefront/views.py, cart_page
context["trust_badge_location"] = "checkout"  # storefront/views_checkout.py, checkout_view
```

`SecurityBadgeSlotProvider.render(context)` reads
`context.get("trust_badge_location")`; a missing/unrecognized value renders
nothing and logs once at `warning` (a location wired to the slot without
setting the key is a developer error to fix, not a shopper-visible failure —
§XV-1: loud in logs, safe on the page).

```python
class SecurityBadgeSlotProvider(SlotProvider):
    slot = "security_badge"
    key = "security_badge"
    name = "Security badges"
    required_settings: list = []

    def is_enabled(self, store) -> bool:
        return True  # gating is per-row/per-location inside render(), same
                      # pattern as PixelsSlotProvider (ADR-022 D3) — one query.

    def render(self, context) -> str:
        request = context.get("request")
        store = getattr(request, "store", None)
        location = context.get("trust_badge_location")
        if store is None or location not in TrustBadgeLocation.values:
            if location is not None:
                logger.warning("SecurityBadgeSlotProvider: unrecognized location %r", location)
            return ""

        rows = SecurityBadge.objects.for_store(store).filter(is_active=True)
        parts = []
        for row in rows:
            loc_cfg = (row.locations_json or {}).get(location)
            if not loc_cfg or not loc_cfg.get("enabled"):
                continue
            try:
                parts.append(_render_one(row, loc_cfg, request))
            except Exception:
                logger.exception("SecurityBadge row=%s failed to render; skipping.", row.pk)
        return mark_safe("".join(parts))

    def validate_settings(self, store) -> list:
        return []  # optional decorative feature — never blocks launch readiness
```

`_render_one()` builds context (`row`, `loc_cfg["width_px"]`, `is_secure`,
`accepted_families`, border fields, `heading_segments_json`) and renders
through `select_template([...]).render(context)` / `render_to_string` —
**never manual string concatenation** of `heading_segments_json` text. That
JSON is store-authored free text; only Django's auto-escaping template
rendering path is trusted with it (unlike `PixelsSlotProvider`, which
concatenates strings that are provider-authored, not shopper- or
merchant-typed — see that file's docstring for the distinction). This closes
an XSS path a naive `f"<span style='color:{color}'>{text}</span>"`
implementation would open.

**Multiple active badges on one location** (screen-inventory LOW item):
render **all** active rows enabled for that location, in creation order
(stacked) — the same "render every enabled provider/row, in order" rule
already used for `pixels` and `bump_*` slots. No special-casing "last one
wins." **ASSUMPTION**, low risk, trivially reversible if a future review
wants a hard single-badge-per-location constraint instead.

**Zero-CLS:** every built-in icon is an inline SVG partial (no network
request, no layout shift). The one exception is `custom_image` (`preset_key
== "custom"`), an `<img>` — the template must set explicit `width`/`height`
attributes (or CSS `aspect-ratio`) from the image's stored dimensions so the
layout is reserved before the byte arrives, matching the platform's existing
zero-CLS discipline for other storefront images.

---

## 5. D5 — Admin: preset picker with live preview

Mirrors the existing precedent exactly:
`engagement.widgets.OverlayThemeSelect` + `engagement.themes.OVERLAY_THEMES`
→ `badges.widgets.BadgePresetSelect` + `badges.badge_presets.BADGE_PRESETS`.
Same `forms.RadioSelect` subclass, same "one real `<label><input
type=radio>` per card, CSS grid instead of `<ul>`" technique, same
`create_option()` override to attach each preset's `preview` SVG path to the
option so the template can render a card with a thumbnail. `preset_key`
gains one extra choice not in the registry, `"custom"`, wired to reveal the
`custom_image` file field and hide the preset grid (small JS toggle,
`vanilla JS + plain CSS`, no build step — matching the T029-B convention
already used by the chat widget).

A genuine **live preview** (the screenshot's right-hand pane rendering the
actual composed badge as style fields change) is **out of scope for this S
ticket** — recommend **PENDING APPROVAL**, default: ship the static
preset-card grid (already visual) without the live re-render-on-keystroke
preview; add live preview as a follow-up ticket if merchants ask for it. This
keeps the ticket at its stated complexity.

Admin gating: `_check_module_access(request, "security_badges", "full")` on
every `ModelAdmin` permission hook (`has_module_permission`,
`has_view_permission`, `has_add_permission`, `has_change_permission`,
`has_delete_permission`) — matching the AF-002 18-module table, where
**"Security Badge"** is listed as its own dedicated row (Full access / No
access only — no "Limited" option, so the level is always `"full"`, same
convention as `engagement`'s `_check_module_access(..., level="full")`
default). This is a **new, dedicated module key**, not a fallback into the
generic `"apps"` bucket that `engagement`/`feeds` use — those two are not
named rows in the 18-module list; Security Badge explicitly is.

List admin mirrors the screenshot: status dot (`is_active`), name, a
computed placement summary string (e.g. "Set on Cart and Checkout" — derived
from `locations_json`, not stored), Edit/Delete actions. `ImageThumbnailMixin`
(`media/admin_mixins.py`) is reused as-is for the `custom_image` list
thumbnail when `preset_key == "custom"`.

---

## 6. D6 — Which pages ship in v1

**All three: product, cart, checkout.** Not "checkout-only" — verified that
`product.html` and `checkout.html` already call `{% render_slot
"security_badge" %}` (ADR-015/ADR-012 groundwork already paid for this), so
the only new template work is the one-line addition to `cart.html`. Shipping
all three in v1 fully satisfies the ticket's acceptance surface
("preset combinations... per-location enable (product page, cart,
checkout)") for the same S-sized effort as checkout-only would have been.

---

## Risks

1. **Payment-logo brand-guideline compliance.** Visa/Mastercard/Amex/
   Discover/PayPal each publish brand-usage guidelines for displaying their
   marks. Using official, unmodified SVGs and only showing a network's mark
   when the store genuinely accepts it (§3) is the standard, defensible
   practice already used industry-wide for this exact purpose — but this
   ADR does not constitute a legal sign-off. **PENDING:** a one-time legal/
   brand check of the five shipped SVG assets before first release; does
   not block writing the code, blocks nothing else in this ADR.
2. **Store-uploaded custom images** carry the same content-liability profile
   as any other merchant upload (product photos, reviews). The admin form
   must show a short disclaimer ("You are responsible for ensuring you have
   the right to use any image you upload here, including third-party
   certification marks.") next to the upload control. No automated
   trademark/content detection is proposed — out of scope for this ADR and
   for an S ticket generally.
3. **Approximate payment-logo derivation** (§3) can show a network logo for
   a family that is platform-enabled but not currently routeable for one
   specific store. Accepted as a decorative-only, narrow inaccuracy; see §3
   for the reasoning and the more-accurate-but-rejected alternative.

## Rollback strategy

The `badges` app is fully additive: one new app, one new table, one new
`SlotProvider` registered into an already-existing, already-rendered slot.
Removing the app (or simply leaving `SecurityBadge.objects.for_store(store)`
empty) makes `render_slot("security_badge")` emit its empty wrapper `<div
data-slot="security_badge"></div>` exactly as it does today on every store
that has never configured a badge — zero regression to any page that does
not use this feature. No other app depends on `badges`.

## Addendum (2026-07-11, wave-3 spec review)

Two implementation decisions were locked in by the shipped code and its tests
but were not written down anywhere in this ADR. Recorded here after the fact
so the spec/ADR trail matches what actually shipped.

1. **`security_badge` is deliberately NOT in `PREVIEW_SUPPRESSED_SLOTS`.**
   `storefront/templatetags/storefront_tags.py` defines
   `PREVIEW_SUPPRESSED_SLOTS = frozenset({"consent", "pixels", "overlay",
   "social_proof", "chat_launcher"})` — every entry in that set is a
   consent/tracking-adjacent slot (analytics pixels, cookie consent, lead
   capture overlays, social-proof popups, chat launchers) that must render
   empty in theme preview so a merchant previewing a theme never triggers a
   real tracking pixel or shows a live popup to themselves. Security badges
   carry no consent or tracking concern — they are static decorative
   markup/images, the same category as the storefront's other always-visible
   chrome. Merchants actively want to see badges while previewing a theme
   (that is the whole point of the D5 preset-picker + preview pane, §5), so
   `SecurityBadgeSlotProvider` is intentionally excluded from the suppression
   set. Pinned by regression test
   `badges/tests/test_slot_provider.py::...assertNotIn("security_badge",
   PREVIEW_SUPPRESSED_SLOTS)`.
2. **`custom_image` zero-CLS uses a fixed 3:1 aspect-ratio, not stored
   per-upload dimensions.** §2's `SecurityBadge` model stores no width/height
   fields for `custom_image` (only `preset_key`, `custom_image`, and the
   generic `locations_json.width_px` display width). D4's "Zero-CLS" note
   said the template "must set explicit `width`/`height` attributes (or CSS
   `aspect-ratio`) from the image's stored dimensions" — the shipped design
   took the CSS `aspect-ratio` branch, not the stored-dimensions branch: the
   model persists no per-upload width/height, and
   `storefront/templates/storefront/partials/security_badges/custom_image.html`
   reserves layout with a hardcoded `aspect-ratio: 3 / 1; object-fit:
   contain` on the `<img>`, mirroring the platform's existing product-image
   zero-CLS convention rather than reading the uploaded file's real pixel
   dimensions on every render. Consequence accepted: a store-uploaded image
   whose native aspect ratio is not 3:1 is letterboxed (`object-fit: contain`
   preserves the image's own proportions inside the reserved 3:1 box rather
   than cropping or stretching it) — recorded in the template as an
   ASSUMPTION (low risk), not a defect.

## Tests required

- Model: `clean()` rejects unknown `preset_key`; rejects `custom_image` set
  without `preset_key="custom"` and vice versa; rejects malformed
  `heading_segments_json` (bad hex color, >6 segments, oversized text);
  rejects unknown location keys / out-of-range `width_px` in `locations_json`.
- Registry: every `BADGE_PRESETS` entry's `template` resolves and renders
  without error given a minimal context; every entry's `preview` static file
  exists (mirrors the equivalent `OVERLAY_THEMES` test, if one exists —
  extend it, do not duplicate the pattern under a new name).
- SlotProvider: renders nothing when `trust_badge_location` is absent/unknown
  (with the expected warning log); renders nothing when no active row is
  enabled for the given location; renders all active+enabled rows in
  creation order when more than one matches (multi-badge stacking, §4);
  never raises when one row's template errors (isolation, matches
  `PixelsSlotProvider` precedent) — the other rows and the wrapper `<div>`
  still render.
- Truthfulness invariants (the point of this ADR — do not skip):
  `lock_shield`/`ssl_seal` never render when `request.is_secure()` is
  `False`; the `card` icon bundle never renders when no `PaymentMethod` row
  with `method_family="card"` is `is_enabled=True`; same for `paypal`.
- XSS: `heading_segments_json` text containing `<script>`/HTML is rendered
  escaped in the output (auto-escape regression test — the exact class of
  bug the manual-string-concatenation approach would have introduced, §4).
- Upload: `custom_image` rejects `image/svg+xml` even if
  `ALLOWED_IMAGE_TYPES` were ever changed platform-wide to include it
  (field-local allow-list test, independent of the global setting).
- Admin: module gating — a `StoreEmployee` with no `security_badges` grant
  (and `full_access=False`) gets `has_module_permission()==False` on every
  hook; a super-admin always passes.
- Template wiring regression: `product.html` and `checkout.html` still call
  the slot (existing regression coverage, if any, extended); `cart.html`
  gains a new assertion that the slot wrapper is present.
