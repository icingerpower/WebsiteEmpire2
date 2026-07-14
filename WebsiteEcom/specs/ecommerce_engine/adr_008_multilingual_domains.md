# ADR-008: Multilingual domain → language → country model

**Status:** ACCEPTED (Architect, 2026-07-03). Resolves **ML-005** (`07_multilingual_seo.md` §1.1) and uncertainty **11:#9**. Unblocks TICKET-016 (final scope), TICKET-024, TICKET-025, TICKET-026, TICKET-033 (feed matrix source). **Complies with design-pattern-ideas §III (single URL resolver), §XI (hreflang from one map), §XV-3 (explicit state), §XV-4 (single resolution function).**

> One deviation from the orchestrator's proposed entity shape (the `StoreDomain.default_language` FK) is documented and justified in *Options considered* §B. Everything else follows the proposed shape.

---

## Decision (summary)

Three new models in the `stores` app — **`StoreDomain`**, **`StoreLanguage`**, **`ShippingCountry`** — plus one shared request-side resolution function **`resolve_locale(host, path)`** and one URL-composition helper **`base_url(store_language)`** consumed by ADR-005's `PermalinkResolver`.

- A **`StoreDomain`** is a hostname registered for a store (globally unique `host`).
- A **`StoreLanguage`** is the (store, language) pair. Because `(store, lang_code)` is unique and each `StoreLanguage` points at exactly one `StoreDomain`, **a `StoreLanguage` row *is* the (domain, language) pair of ML-003** — no join table needed.
- **Country lives on `StoreLanguage`** (via `ShippingCountry` rows) — not on the domain, not in a per-store shipping matrix. See *Options considered* §A.
- The **domain's default language** (ML-002: served at `/`, no prefix) is the unique `StoreLanguage` on that domain with `use_path_prefix=False`, enforced by a DB partial unique constraint — not a FK on `StoreDomain`. See *Options considered* §B.
- Countries are **never** in the URL (ML-003). The permalink tables (`Permalink`, `SlugRedirect`) are **unchanged** — `lang` stays a plain code string.

---

## Context

- **ML-001/ML-002** require both configurations to coexist within one store: dedicated single-language domains (`pradize.fr` = FR) and multi-language domains with path prefixes (`pradize.com/` = EN, `pradize.com/fr/` = FR). Each domain has exactly one language at its root.
- **ML-003** requires each (domain, language) pair to target ≥1 countries — driving feeds (one feed per provider × country × language, ML-030/FEED-001) and hreflang availability (CAN-004: translated ∩ available countries) — with no country in URLs.
- **ML-004/URL-003** require a *single* shared resolution function `host + path prefix → (store, language)`; no component may re-derive language from the URL independently (§III was a production bug class in WebsiteEmpire2).
- **ML-010** shapes the admin flow: add a language = choose (a) code, (b) domain + prefixed-or-dedicated, (c) target countries.
- **ML-011/ML-041** — only `published` translations are routable; adding a language exposes nothing until translations publish.
- **ML-012** — disabling a language serves 410, keeps redirects.
- Existing code: `Store` has `subdomain`, `custom_domain`, `primary_language` (single-domain, single-language assumption baked in); `Permalink` already carries `(store, lang, slug)` with per-language uniqueness (ADR-005) and needs only a URL-prefixing rule, not a schema change.

---

## Options considered

### A. Where does "country" live?

1. **On `StoreDomain`** — rejected. A multi-language domain has per-language markets (EN → US+CA, FR → FR+BE on the same `.com`); a domain-level list cannot express ML-030's per-(language, country) feed matrix.
2. **Per-store shipping matrix (reuse shipping zones, TICKET-032)** — rejected as the *source of truth* for this concern. Shipping zones answer "can we physically ship there and at what rate" (operational). ML-003 countries answer "which markets does this language edition target" (marketing/SEO/feeds) — a deliberate admin choice per ML-010(c). Coupling them would make an admin unable to run an EN edition targeting US only while zones cover 40 countries. They stay related but distinct; a launch check cross-warns (see Risks).
3. **On `StoreLanguage` via `ShippingCountry` rows** — **chosen.** `StoreLanguage` ≡ the (domain, language) pair (unique `(store, lang_code)` + one domain FK), so this is exactly ML-003's attachment point. Feeds iterate `ShippingCountry` per language; hreflang intersects it per CAN-004.

### B. How is the domain's default language modelled?

1. **`StoreDomain.default_language = FK(StoreLanguage)`** (orchestrator's proposed shape) — rejected. It is circular (`StoreLanguage.domain → StoreDomain → StoreLanguage`), forcing a nullable FK and two-step object creation, and it duplicates the fact already expressible by `use_path_prefix` — two sources of truth that *will* drift (a language flagged prefix-free on domain X while X's FK points at another language is representable and silently wrong — the §XV-1 invisible-failure class).
2. **Single source of truth: `use_path_prefix=False` ⟺ "served at this domain's root" ⟺ "this domain's default language"** — **chosen**, enforced by a DB partial unique constraint (at most one root language per domain). `StoreDomain.default_language()` is provided as a query helper so the accessor the orchestrator wanted still exists — as a method, not a column.

Corollary that also corrects the proposed semantics: `use_path_prefix=False` does **not** imply a single-language domain. Per ML-002 the default language of a *multi*-language domain is also unprefixed. A "dedicated domain" is simply a domain with exactly one `StoreLanguage` (which necessarily has `use_path_prefix=False`).

### C. Should `Permalink.lang` become a FK to `StoreLanguage`?

Rejected. `Permalink` is a high-volume hot table; lang codes are stable two-letter identifiers (identity, not presentation); a FK adds join cost and a hard coupling ADR-005 deliberately avoided. Write-time validation (permalink save checks `lang ∈ store's StoreLanguage codes`) gives the integrity without the coupling. Consistent with the soft-ID philosophy of ADR-004.

### D. New app vs `stores` app

`stores` app — chosen. These are infrastructure/routing models resolved *before* store scoping exists (host lookup is inherently cross-store), so they must **not** extend `StoreOwnedModel` — same documented exception as `StoreEmployee` (see `stores/models.py` module docstring, ADR-001). A separate `domains` app would split the Store aggregate for no isolation benefit.

---

## §1 — Data model (exact definitions)

All three models live in `stores/models.py`. Plain `models.Model` + plain managers (infrastructure exception, ADR-001 §4 note above). All are managed at `/admin/` Settings by the store admin; `host` creation may additionally be gated by super-admin policy later (out of scope here).

```python
class StoreDomain(models.Model):
    """
    A hostname registered for a store (ML-001/ML-002).

    host is globally unique — request routing maps Host header → exactly one store.
    Always stored lowercase, no scheme, no port, no trailing dot (normalized in save()).
    A store's platform subdomain (e.g. "mystore.pradize.com") is ALSO a StoreDomain row
    (created by the ADR-008 data migration) so that request resolution has ONE mechanism.

    is_primary: the store's canonical domain. Exactly one per store (partial unique).
    Used as the hreflang x-default host when the default language lives on it, and as
    the target of parked-domain redirects (SlugRedirect trigger=domain_park).

    The domain's default language (ML-002) is NOT a column here — it is the unique
    StoreLanguage on this domain with use_path_prefix=False (see ADR-008 Options B).
    """

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.CASCADE,
        related_name="domains",
    )
    host = models.CharField(
        max_length=253,  # RFC 1035 FQDN limit
        unique=True,
        help_text="Lowercase hostname, no scheme/port — e.g. 'mystore.fr', 'shop.mystore.com'.",
    )
    is_primary = models.BooleanField(
        default=False,
        help_text="The store's canonical domain. Exactly one per store.",
    )
    is_active = models.BooleanField(default=True)
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "store domain"
        verbose_name_plural = "store domains"
        constraints = [
            models.UniqueConstraint(
                fields=["store"],
                condition=Q(is_primary=True),
                name="unique_primary_domain_per_store",
            ),
        ]

    def default_language(self):
        """The StoreLanguage served at this domain's root (ML-002), or None if misconfigured.

        None is a launch-blocking misconfiguration (settings validation), never silent."""
        return self.languages.filter(use_path_prefix=False).first()
```

```python
class StoreLanguage(models.Model):
    """
    One (store, language) pair with its routing configuration (ML-005).

    Because (store, lang_code) is unique and each row binds to exactly one domain,
    a StoreLanguage row IS the (domain, language) pair of ML-003 — target countries
    (ShippingCountry) and feed matrices hang off this row.

    use_path_prefix:
      False → served at the domain root ('/'): this IS the domain's default language
              (ML-002). At most one per domain (DB partial unique constraint).
      True  → served under '/<lang_code>/' on the domain (e.g. '/fr/').

    is_default: the store's default language — hreflang x-default target (CAN-006).
    Exactly one per store (partial unique + launch-readiness check for existence).

    is_enabled: disabling (never deleting) a published language serves 410 Gone for
    its whole URL namespace and removes it from sitemap/hreflang/feeds (ML-012).
    Redirect entries are kept.

    Routability (ML-011) is NOT this model's job: a language being enabled exposes
    nothing by itself — only 'published' translation records make URLs resolve.
    """

    LANG_CODE_VALIDATOR = RegexValidator(
        r"^[a-z]{2}(-[a-z]{2})?$",
        "Lowercase ISO 639-1 code, optionally with a region suffix — e.g. 'en', 'pt-br'.",
    )

    store = models.ForeignKey(
        "stores.Store",
        on_delete=models.CASCADE,
        related_name="languages",
    )
    domain = models.ForeignKey(
        StoreDomain,
        on_delete=models.PROTECT,
        related_name="languages",
        help_text="The domain this language edition is served from.",
    )
    lang_code = models.CharField(
        max_length=10,
        validators=[LANG_CODE_VALIDATOR],
        help_text="ISO 639-1 code — doubles as the URL path prefix when use_path_prefix=True.",
    )
    use_path_prefix = models.BooleanField(
        help_text=(
            "False = served at the domain root (this is the domain's default language, "
            "ML-002). True = served under /<lang_code>/ on the domain."
        ),
    )
    is_default = models.BooleanField(
        default=False,
        help_text="The store's default language (hreflang x-default). Exactly one per store.",
    )
    is_enabled = models.BooleanField(
        default=True,
        help_text="Disabled languages serve 410 Gone (ML-012); rows are never deleted.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "store language"
        verbose_name_plural = "store languages"
        constraints = [
            models.UniqueConstraint(
                fields=["store", "lang_code"],
                name="unique_lang_per_store",
            ),
            models.UniqueConstraint(
                fields=["domain"],
                condition=Q(use_path_prefix=False),
                name="one_root_language_per_domain",
            ),
            models.UniqueConstraint(
                fields=["store"],
                condition=Q(is_default=True),
                name="one_default_language_per_store",
            ),
        ]

    def clean(self):
        # Cross-tenant guard: the domain must belong to the same store.
        # Not expressible as a DB constraint (requires a join) — enforced here and
        # covered by a dedicated test (invisible-failure class, §XV-1).
        if self.domain_id and self.domain.store_id != self.store_id:
            raise ValidationError("domain must belong to the same store.")
```

```python
class ShippingCountry(models.Model):
    """
    A target country for a (store, language) pair — ML-003.

    Drives: shopping feed matrix (one feed per provider × country × language,
    FEED-001/ML-030) and hreflang availability (CAN-004: hreflang emitted only for
    translated ∩ targeted). Countries NEVER appear in URLs (ML-003).

    NOT the shipping-rates model: operational shippability/rates come from shipping
    zones (TICKET-032). This is the admin's declared target-market list (ML-010c).
    A settings-validation check warns when a target country has no shipping-zone
    coverage (divergence is visible, never silent).
    """

    store_language = models.ForeignKey(
        StoreLanguage,
        on_delete=models.CASCADE,
        related_name="shipping_countries",
    )
    country_code = models.CharField(
        max_length=2,
        validators=[RegexValidator(r"^[A-Z]{2}$", "ISO 3166-1 alpha-2, uppercase.")],
        help_text="ISO 3166-1 alpha-2 — e.g. 'US', 'FR'.",
    )
    created_at = models.DateTimeField(auto_now_add=True)

    class Meta:
        verbose_name = "shipping country"
        verbose_name_plural = "shipping countries"
        constraints = [
            models.UniqueConstraint(
                fields=["store_language", "country_code"],
                name="unique_country_per_store_language",
            ),
        ]
```

### Invariants (and how each is enforced)

| Invariant | Requirement | Enforcement |
|---|---|---|
| One store per hostname | routing | `host` `unique=True` |
| One canonical domain per store | hreflang x-default host, parking target | partial unique `unique_primary_domain_per_store` |
| One language per (store, code) | ML-003 pairing | unique `(store, lang_code)` |
| At most one root language per domain | ML-002 | partial unique `one_root_language_per_domain` |
| At most one store default language | CAN-006 | partial unique `one_default_language_per_store` |
| Domain belongs to the language's store | tenant isolation | `clean()` + test (join constraint, not DB-expressible) |
| *Exactly* one root language per active domain; *exactly* one default per store; ≥1 country per enabled language | launch quality | settings-validation / launch-readiness checks (`06_settings_requirements.md`) — a domain cannot be activated for a storefront launch while any check fails |
| Untranslated pages never exposed (ML-011) | routing | **not** this model — `published` translation status gates `Permalink` resolution (ADR-005/TICKET-024) |

### Reserved-slug addition (feeds into URL-001 / TICKET-016)

Any slug matching `^[a-z]{2}(-[a-z]{2})?$` **that is a valid ISO 639-1 code** is rejected by slug validation — even for codes not currently enabled — so that adding a language later can never collide with an existing root-level slug (e.g. a product slugged `fr` shadowing the `/fr/` prefix). This extends URL-001's reserved list; TICKET-016 freezes it.

---

## §2 — URL resolution (ML-004)

Two functions, one module (`permalinks/resolver.py` alongside ADR-005's `PermalinkResolver`). **Nothing else in the codebase may map hosts/prefixes to languages or compose language-prefixed URLs.**

### 2a. Request side — `resolve_locale`

```python
@dataclass(frozen=True)
class RequestLocale:
    store: Store
    domain: StoreDomain
    language: StoreLanguage
    path: str  # original path with the language prefix stripped; leading slash kept

def resolve_locale(host: str, path: str) -> RequestLocale:
    """Single shared host+prefix → (store, language) resolution (ML-004).
    Raises Http404 (unknown host / no root language) or GoneError (disabled language)."""
```

Algorithm (exactly one DB lookup path, cached):

1. **Normalize host**: lowercase, strip `:port`, strip trailing dot. No `www.`-stripping magic — a `www.` variant is either its own `StoreDomain` row or a parked-domain redirect (`SlugRedirect` trigger `domain_park`, ADR-005 §6).
2. **`StoreDomain` lookup** by `host` (`is_active=True`, `select_related("store")`, store not soft-deleted). Miss → check the parked-domain redirect table → else **404**. The `host → (domain, its languages)` map is cached per process and invalidated on the same store+epoch signal path as the `PermalinkResolver` cache (ADR-005 §3 — one invalidation path, §XV-4).
3. **Prefix match**: take the first path segment. If it equals the `lang_code` of a `StoreLanguage` on this domain with `use_path_prefix=True` → that language; `path` = remainder (`/fr/robe-rouge/` → `/robe-rouge/`). The match is against **actual rows**, never a bare two-letter regex (a `/xx/` segment for a non-registered code falls through to step 4 and 404s as an unknown slug).
4. **No prefix match** → the domain's root language (`use_path_prefix=False`); `path` unchanged. No root language on an active domain → 404 + error-level log (launch checks make this unreachable in a launched store; if reached, it must be loud — §XV-1).
5. **Enabled gate**: `language.is_enabled=False` → **410 Gone for the entire namespace of that language** (ML-012). Decision: 410 applies to *all* URLs of the disabled language, not only previously-published ones — indistinguishable to crawlers on never-published URLs (they were 404 before) and it avoids a per-request publication-history lookup. Redirect rows are untouched.
6. Return `RequestLocale`. The storefront middleware calls this **once per request**, sets `request.locale`, and everything downstream (permalink lookup with `(store, language.lang_code, slug)`, templates, SEO block) reads it. `request.store` (ADR-001 middleware) is derived from the same call — one resolution, not two.

`/fr` without trailing slash 301s to `/fr/` (URL-002, handled with the general trailing-slash rule).

### 2b. Content side — URL composition consumed by `PermalinkResolver`

```python
def base_url(language: StoreLanguage) -> str:
    """'https://mystore.fr' or 'https://mystore.com/fr' — the ONLY place scheme,
    host and language prefix are composed."""
```

ADR-005 §3's map `(content_type, object_id, lang) → url` now emits **absolute URLs**: `base_url(language) + "/" + slug + "/"` (collections: `+ "collections/" + slug + "/"`). Absolute is required anyway by CAN-003 (canonical) and cross-domain hreflang (a store's FR edition on `mystore.fr` and EN on `mystore.com` must reference each other's full URLs). Sitemap children, feeds (`link`), hreflang, canonicals, menus, emails all keep consuming the resolver map — ADR-008 changes what the map's values look like, **not** the ADR-005 interface, exactly as TICKET-016's blocker note predicted.

Map membership (routability) is the conjunction: language `is_enabled` **AND** translation status `published` (ML-011/ML-041) **AND** page-type indexability gates where relevant. Disabled languages are therefore absent from sitemap/hreflang/feeds *by construction* while the request path serves their 410.

`resolve_permalink(source_object_or_url, target_lang)` (URL-003 naming) remains the spec-level contract; `PermalinkResolver` + `base_url` is the canonical implementation (naming note already recorded in 07 §SEO-review item 4).

---

## §3 — Admin surface (describe only — implemented in TICKET-024)

Location: Settings → **General Settings + Domains** merged screen (admin-016, ML-010). Two tables, mirroring the WebsiteEmpire2 "Domains" tab:

**Domains table** (one row per `StoreDomain`): Host · Primary badge · Active toggle · Root language · #languages. Adding a custom domain here creates the row (DNS/TLS verification flow is a later ticket, cf. TICKET-040's pattern).

**Languages table** (one row per `StoreLanguage`) — columns:

| Column | Content |
|---|---|
| Language | `lang_code` + display name (e.g. "fr — Français") + store-default badge (`is_default`) |
| Domain | the `StoreDomain.host` |
| Routing | "domain root" (`use_path_prefix=False`) or "path prefix `/fr/`" |
| Enabled | toggle; disabling warns "previously published URLs will return 410 Gone" (ML-012) |
| Target countries | country chips (the `ShippingCountry` set), inline-editable |
| Translation coverage | % of translatable objects with `published` status in this language (reads TICKET-024 translation records; the column that makes ML-011 visible) |

**Add Language** button → modal with exactly ML-010's three choices: (a) language code select; (b) domain: existing domain (→ path-prefixed) or new dedicated host (→ creates `StoreDomain`, language becomes its root); (c) target-country multi-select. On save the language is enabled but exposes nothing (zero published translations — ML-011); a translation job (highest priority, ADR-003) can be queued from the same screen.

Languages are **disable-only** in store admin — no delete (410 + redirect history must survive; hard delete is super-admin, and only for never-published languages).

**Launch-readiness** (settings-validation architecture, `06_settings_requirements.md`): blocking — every active domain has a root language; the store has exactly one default language; warning — an enabled language has zero target countries (blocks feed generation only), a target country lacks shipping-zone coverage.

---

## §4 — Migration strategy

Purely **additive** — no existing table is altered, no existing row is touched destructively.

1. **Schema migration** (`stores`): create `StoreDomain`, `StoreLanguage`, `ShippingCountry` with the constraints above.
2. **New Django setting** `PLATFORM_APEX_DOMAIN` (e.g. `"pradize.com"`) — required by the data migration to compose subdomain hosts. Missing setting fails the migration loudly.
3. **Data migration** (reversible), per `Store` row (including soft-deleted; domain `is_active` mirrors `store.is_active`):
   - Create `StoreDomain(host=f"{subdomain}.{PLATFORM_APEX_DOMAIN}")`.
   - If `custom_domain` is set: create a second `StoreDomain(host=custom_domain, is_primary=True)`; the subdomain row stays active, non-primary. Otherwise the subdomain row is primary.
   - Create `StoreLanguage(lang_code=store.primary_language, domain=<primary domain>, use_path_prefix=False, is_default=True, is_enabled=True)`.
   - **No `ShippingCountry` rows** — there is no source data to derive them from; the launch-readiness warning surfaces the gap per store instead of inventing markets silently.
   - Reverse operation: delete the three tables' rows (no other state was written).
4. **`Store.subdomain` / `custom_domain` / `primary_language` are retained** as denormalized legacy fields (checkout/email code reads `primary_language`; provisioning writes `subdomain`). A `post_save` sync on the default `StoreLanguage` keeps `Store.primary_language` equal to its `lang_code`, and a system check flags divergence. **New code must read `StoreDomain`/`StoreLanguage`** — the fields' help_text gains a deprecation note. Removal is a later cleanup ticket once no readers remain.
5. **`Permalink` / `SlugRedirect`: zero changes.** `lang` stays a code string; a write-time validation (permalink save) asserts `lang` matches a `StoreLanguage` of the store (Options C).

---

## §5 — Sequencing (tickets unblocked)

| Order | Ticket | ADR-008 impact |
|---|---|---|
| 1 | **TICKET-016** (Phase 1) — blocker **cleared** (this ADR is the "design doc for sign-off" its PENDING note demanded). Scope now includes: the three models + constraints, `PLATFORM_APEX_DOMAIN` + data migration, `resolve_locale` + locale middleware, `base_url` composition in `PermalinkResolver` (absolute URLs), ISO-lang-code reserved-slug rule, disabled-language 410 handling. |
| 2 | **TICKET-024** — blocker cleared. Adds translation records + publication gating (ML-011) into the resolver map, and ships the §3 admin surface (Languages/Domains tables need the coverage column, hence this ticket, not T016). |
| 3 (parallel) | **TICKET-025** (SEO metadata: hreflang = resolver map ∩ `ShippingCountry`, x-default from `is_default`) and **TICKET-026** (sitemap index per domain, one child per **enabled** language on that domain; robots.txt per domain — ROB-003). Both depend only on T016+T024. |
| later | **TICKET-033** (feeds): the feed matrix iterates `ShippingCountry` per `StoreLanguage` (FEED-001). **TICKET-042** (A/B variants) unaffected. AMZ-004 (per-country Amazon tag) now has an attachment point (domain root language's countries) but remains PENDING. |

---

## Risks

- **`ShippingCountry` vs shipping zones divergence** — a language can target CA while no zone ships there (feed items would all be excluded per FEED-003/FEED-004). Mitigated by the launch-readiness cross-check warning; accepted as two deliberately distinct concepts.
- **Legacy field drift** (`Store.primary_language` vs `StoreLanguage.is_default`) — mitigated by sync signal + system check; residual risk until the cleanup ticket removes the readers.
- **Host cache staleness** — a newly added domain must resolve promptly; mitigated by sharing the ADR-005 epoch-invalidation signal path (one code path, tested).
- **`clean()`-only cross-store guard** — bulk ORM writes bypassing `full_clean()` could bind a language to another store's domain. Mitigated by a dedicated test and by the admin/service layer being the only writers; if it ever bites, escalate to a DB trigger.
- **410-for-whole-namespace** slightly over-broadens ML-012 (never-published URLs of a disabled language also get 410 instead of 404) — accepted, documented in §2a step 5; crawler-behavior difference is nil.

## Rollback strategy

- Tables are additive: rolling back = reverse the data migration + drop the three tables; `Store`'s legacy fields still hold everything single-language operation needs, and the pre-ADR-008 host middleware (subdomain/custom_domain lookup) is restorable from the same commit.
- The locale middleware ships behind a settlement period in which `resolve_locale` and the legacy `Host → Store` resolution are asserted to agree for all existing stores (log-only mismatch counter, §XV-1) before the legacy path is deleted.
- Rule/config changes (enable/disable language, countries) are live-on-save; rollback = revert the toggle. Disabling is always reversible because rows are never deleted.

## Tests required

Model layer:
- Constraint tests: duplicate `host` rejected; second `is_primary` domain per store rejected; second `(store, lang_code)` rejected; second `use_path_prefix=False` language on one domain rejected; second `is_default` language per store rejected.
- Cross-store guard: `StoreLanguage.clean()` rejects a domain of another store.
- `ShippingCountry` uniqueness per `(store_language, country_code)`.

Resolution (black-box, per 07 §12):
- `resolve_locale`: host case/port normalization; prefixed path → prefixed language + stripped path; unknown two-letter first segment falls through to root language; unknown host → 404; domain without root language → 404 + error log; disabled language → 410 for prefixed and dedicated-domain cases (**TST-ML-012**).
- Reserved-slug rule: slug `fr` rejected even when FR is not enabled for the store.
- Single-resolver property: hreflang, sitemap alternates and breadcrumb URLs byte-identical per language across two domains of one store (**TST-URL-003b**), including a dedicated-domain + path-prefix mixed configuration (ML-001 coexistence).
- `base_url`: dedicated domain, prefixed language, and store-default (x-default, **TST-CAN-006**) compositions.
- hreflang ∩ countries: product published EN/FR/DE, shipped FR+DE only → hreflang {fr, de, x-default} (**TST-CAN-004**).
- ML-011: `pending` translation → 404, absent from sitemap/hreflang (**TST-ML-011**).

Migration:
- Data migration on a store with and without `custom_domain` produces the expected domain/language rows (primary flags, root language); reverse migration restores a clean slate; missing `PLATFORM_APEX_DOMAIN` fails loudly.
- Legacy-sync: changing the default `StoreLanguage.lang_code` updates `Store.primary_language`.

## Consequences

- ML-005, the last blocker on the multilingual chain, is closed; T016's final scope is frozen and T024/T025/T026 have a concrete schema to build on.
- The (domain, language) pair has exactly one representation (`StoreLanguage`), countries exactly one attachment point, and URL prefixing exactly one composition function — wrong-language and wrong-host links stay structurally impossible (§III) rather than review-dependent.
- Cost: two legacy `Store` fields linger with a sync shim until a cleanup ticket; one more cached lookup (host map) sharing the existing invalidation path.
- Spec follow-ups for the Spec Agent: mark ML-005 DECIDED in `07_multilingual_seo.md` (§1.1 + open-items table) referencing this ADR; record the ISO-code reserved-slug addition under URL-001; note the §2a-step-5 broadened-410 decision under ML-012.
