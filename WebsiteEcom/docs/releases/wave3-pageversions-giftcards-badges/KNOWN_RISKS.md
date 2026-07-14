# Known Risks — Wave 3: Page Versions, Gift-Card Campaigns, Security Badges + Isolation-Hardening Chain

**Date:** 2026-07-11

Every item below was either independently re-verified in code by this release pass, or is
carried forward from a prior audit/review document that this pass located and read directly.
Where re-verification was not possible from an existing artifact, that is stated.

---

## Human / legal decisions required (not blocking this release unless noted)

| # | Item | Owner | Blocking? | Detail |
|---|---|---|---|---|
| 1 | Payment-network SVG placeholders — one-time legal/brand check | Human / legal | **Blocks first real store enabling badges**, not this release | ADR-030 Risk #1: badge payment-network logos are derived live from each store's enabled `PaymentMethod.method_family` rows, but the underlying logo assets themselves need a one-time legal/brand sign-off before any store's storefront actually renders them to shoppers. |
| 2 | Legal track: beacon exemption, named social-proof modes, DE opt-in | Human / legal | No (carried, pre-existing, unrelated to wave 3's own gate) | Carried from the feeds-and-overlays release: named (non-anonymous) social-proof display modes remain code-flagged inert platform-wide pending legal/privacy sign-off; German-market double opt-in default is implemented but its legal sufficiency has not been independently re-reviewed in this pass (out of wave-3's scope — no wave-3 code touches this path). |
| 3 | W3-5 — flatten gift-card email-template context before any `owner_scope='store'` campaign ships | Developer (future ticket) | **Blocks that future ticket, not this release** (v1 only allows `owner_scope='platform'`) | `docs/security/WAVE3_AUDIT.md` §1.6: admin-authored subject/body templates render with the full `order`/`store` model instances in context. Django templates cannot execute code, and `StoreOwnedModel` related managers raise on unscoped traversal, so exposure today is minimal — but this is explicitly a hard requirement before the `owner_scope` door (deliberately left open in the schema) is ever opened to store admins. |
| 4 | Parent `.gitignore` bare `tests` pattern swallows every Django `tests/` directory | Human | **Must be fixed before WebsiteEcom is ever `git init`'d as its own repo** | **Independently re-verified by this pass**, not just carried: `git check-ignore -v WebsiteEcom/discounts/tests/test_discount_admin_inline_isolation.py` from the parent (`WebsiteEmpire2`) repo root returns `.gitignore:2:tests` — a match. WebsiteEcom currently lives as an untracked subtree inside the parent git repo (confirmed: `git status` from WebsiteEcom shows the parent repo's unrelated C++ file changes, not WebsiteEcom's own history). **This means every Django `tests/` directory across the entire WebsiteEcom codebase — including every BUG_TESTS.csv-referenced regression test proven in this and prior releases — would be silently excluded the moment WebsiteEcom is committed**, unless the parent `.gitignore` line 2 is fixed (e.g. narrowed to a specific path or removed) first. This is a silent, total test-suite-loss risk, stated prominently as instructed. |

## Carried LOW/INFO safety findings (per `docs/security/WAVE3_AUDIT.md`, all scheduled hardening, none release-blocking)

| ID | Area | Detail | Owner |
|---|---|---|---|
| W3-5 | Gift cards / emails | See human decision #3 above. | Developer, future |
| W3-6 | Gift cards | `recipient_email` is part of the idempotency unique key — a replay that observes a changed `order.customer_email` between deliveries can double-issue. Recommend: pre-check `campaign_id` alone inside the row lock. | Developer, backlog |
| W3-7 | Gift cards | FR 5-year floor is enforced at `.clean()`/publish time only; inert on a hypothetical future programmatic `.save()` path; no issuance-time re-check if a campaign's market composition changes after publish. | Developer, backlog |
| W3-8 | Gift cards / admin | `trigger_products` cross-store selection is possible in the super-admin form and silently never matches at runtime (an operator-confusion risk, not a privilege escalation). Recommend: validate store consistency in `GiftCardCampaignAdminForm.clean()`. | Developer, backlog |
| W3-9 | Badges | `border_color` lacks the same hex-format validation `heading_segments_json` colors get. Autoescape + 7-char cap make it practically inert (worst case: broken CSS on the store's own page). | Developer, backlog |
| W3-10 | Badges / deployment | Media serving hardening for `custom_image`: add `FileExtensionValidator` at the model layer (not just the form layer), and production must serve `/media/` with `X-Content-Type-Options: nosniff`. **See deploy-time item below — this is partly a deploy task.** | Developer (model validator) + Ops (nosniff header) |
| W3-11 | Analytics beacon | No per-IP rate limit on `/analytics/beacon/`; client-supplied `created_at` accepted as-is; `properties` size unbounded per event. Pre-existing, amplified by page-version tracking's higher event volume potential. | Developer, backlog |
| W3-12 | Gift cards | `IntegrityError` from a `(store, code)` collision inside the issuance savepoint would be misread as "already issued" and silently drop that order's issuance. Negligible probability at 64-bit code entropy; documented so the uniqueness pre-check is never "optimized away." | Developer, backlog (documentation only) |
| W3-13 | Badges | `accepted_families` cache (300s, platform-wide) means a just-disabled payment processor keeps being advertised on badges for up to 5 minutes; also platform-level derivation means a store with no healthy account for an otherwise-enabled family still shows that logo. Explicitly accepted per ADR-030 §3. | Accepted, no action required |

## Deploy-time checklist items (environment tasks, not code tasks)

| Item | Detail | Owner |
|---|---|---|
| Media `/media/` nosniff header | Production must serve `/media/` with `X-Content-Type-Options: nosniff` and extension-derived Content-Type once the storage backend (S3 block, currently commented out in `webecom/settings/production.py`) is finalized. Ties to W3-10. | Ops / Developer at deploy time |
| Frequency-cap query `EXPLAIN` on Postgres | The gift-card frequency-cap query (`campaign_id LIKE 'gift_card_campaign:N:%'`) should be `EXPLAIN`'d against production Postgres; add a `varchar_pattern_ops` index on `campaign_id` if it seq-scans. Not a security issue — a performance item under load. | Ops, at/after first production deploy |
| Real production `SECRET_KEY` | `check --deploy` re-run by this pass with a placeholder key correctly flagged `security.W009`. A properly generated, long, random `SECRET_KEY` must be set in the real production environment — this is expected/standard, not a wave-3-specific finding. | Ops, at deploy time |
| `PLATFORM_APEX_DOMAIN`, `DATABASE_URL`, `CACHE_URL`, `ANTHROPIC_API_KEY`/`CHAT_KILL_SWITCH` | All confirmed to fail loudly (`ImproperlyConfigured`) at boot if unset — verified by triggering each guard during this pass's `check --deploy` run. Standard deploy-time environment configuration, carried from prior releases, re-confirmed still correct. | Ops, at deploy time |
| Carried items from prior releases | Feed-file storage location, lead-capture consent tooling, etc. — see the respective prior release's `KNOWN_RISKS.md`/`ROLLBACK_PLAN.md` under `docs/releases/feeds-and-overlays/`, `docs/releases/consent-management/`, `docs/releases/pixels-currency-chat/`. Not re-audited in this pass; no wave-3 code touches those areas. | Per prior release documents |

## Other backlog items (LOW, non-blocking)

| Item | Detail |
|---|---|
| Primary-page JSON-LD http/https scheme inconsistency | **Independently re-verified in code by this pass** (not just carried as a claim): `permalinks/resolver.py::base_url()` hardcodes `https://` unconditionally, while the **primary** product page's JSON-LD `offers.url` (`storefront/views.py:567-570`) is built from `request.build_absolute_uri()`, which reflects the actual incoming request scheme. If a production deployment's TLS-terminating proxy does not correctly set `SECURE_PROXY_SSL_HEADER` (so `request.is_secure()` returns `False` on an actually-HTTPS request), the primary page's JSON-LD could emit `http://` while its own canonical tag, hreflang tags, and sitemap entries (all built via `base_url()`) emit `https://` for the same page — a same-page signal mismatch. Version pages do not have this problem after SEO FIX-1 (they now also route through `base_url()`/`seo_canonical_path`). Low severity: requires a proxy misconfiguration to manifest; recommend unifying the primary-page path onto `base_url()` too, in a future backlog ticket. |
| CsvListWidget display polish | `prepare_value` is fixed for lists (ADR-032 D5) — `tags`/`coverage_areas_json` now render as `red, blue` instead of `["red", "blue"]`, and the no-op-save corruption (`tags=['[]']`) is fixed. Residual display roughness: no specific additional roughness was found or reported beyond what ADR-032's own D5 notes describe (the fix note does not mention any remaining cosmetic gap); this item is listed to explicitly close the loop on the checklist prompt rather than to flag a new defect. |

## Spec-review residuals (per orchestrator's gate state; see `RELEASE_CHECKLIST.md` for the documentation gap noted against this)

| Item | Detail |
|---|---|
| Live-JS preview descoped | Documented in ADR-030's own docstring as out-of-scope for v1, PENDING follow-up — not a defect, an explicit scope decision. |
| Unspent-void audit note | Gift-card campaigns: an audit-trail gap around the unspent-voiding path is documented as a known gap in the spec-review record, not fixed in this release. Not independently located as a standalone artifact by this pass — carried as reported. |
| Midnight-flake test | A test was pinned with an honest note that the originally-observed failure could never be reproduced/matched on re-run — most consistent with ordinary date/time-boundary test flakiness rather than a functional defect, but flagged transparently rather than silently dropped. Not independently reproduced by this pass (the full suite ran clean at 3463/3463 during this pass's own run). |

---

## Summary

None of the items above block the READY verdict for wave 3 as a whole. Item #4 (parent
`.gitignore`) is the one item in this document with consequences beyond this release — it is a
latent, silent, total test-suite-loss risk for the day WebsiteEcom is first committed to its own
git history, and is stated here prominently per instruction, independent of whether it affects
this release's deployability (it does not — WebsiteEcom is not currently version-controlled as
its own repository, so no test file has actually been lost yet).
