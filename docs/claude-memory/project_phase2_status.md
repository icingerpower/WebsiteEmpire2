---
name: project-phase2-status
description: "Phase 2 start state — what's running, what needs human approval tomorrow"
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
---

Phase 1 declared COMPLETE 2026-07-03 (Release Manager). 616 tests green.

Phase 2 started 2026-07-03 overnight.

**Running agents (will complete while user sleeps):**
- Architect → ML-005 ADR (StoreLanguage/StoreDomain/ShippingCountry) → `specs/ecommerce_engine/adr_008_multilingual_domains.md`
- Developer → T022 (Transactional email system, single-language v1)
- Developer → multi-select widget for currencies/countries (nearly done)

**Needs human approval tomorrow (do NOT implement before approval):**
- ML-005 ADR: must be reviewed before any multilingual domain models are built
- T022 output: review before marking complete

**Phase 2 ticket order (from 10_implementation_tickets.md):**
T022 (email) → T021 (abandoned checkout) → T024 (multilingual pipeline, after ML-005 approved) → T025 (SEO) → T026 (sitemap) → T027/T028 (upsell funnel, cleared by Safety) → T029 (storefront themes)

**Why:** T021 depends on T022; T024 needs ML-005 ADR approved; T025/T026 depend on T024; T029 (themes) is the big "first sellable" milestone.

**Fast-follow items from Phase 1 safety review (non-blocking, track before named milestones):**
- M1: webhook order-account cross-org check → before T028
- L1: EncryptedCharField decrypt failure logging → before first production processor
- L4: FERNET_KEY startup validation → before first production deploy
- M3: per-email coupon concurrency → before high-volume launch
- M4: beacon field validation → before Phase 2 analytics launch
- L3: anonymous session cap → before public storefront launch
