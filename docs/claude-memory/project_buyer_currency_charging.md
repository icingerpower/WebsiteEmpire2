---
name: project-buyer-currency-charging
description: "Charge currency model — ADR-062 base-only charge amended by ADR-064 buyer-currency charging (flag OFF, cleared to enable)"
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-04T06:17:56.984Z
---

Charge-currency model for Pradize (WebsiteEcom), settled 2026-09-04.

- **Display currency** (ADR-063): presentation only, `currency/session.py resolve_display_currency` (+ `currency/autodetect.py` VPN-robust auto-default, geo behind `CURRENCY_GEO_AUTODETECT_ENABLED`, default OFF, DPO-gated).
- **Charge currency**: ADR-062 (`62835e1`) first made `Order.currency` authoritative = store base (`resolve_store_charge_currency`). **ADR-064 (`59860c5` + enablement `1ccb49c`) AMENDS that**: when `BUYER_CURRENCY_CHARGING_ENABLED` (default **OFF**) is on, charge in the buyer's DISPLAY currency IF the routed provider supports it, else base. Charge = the displayed total, rate locked on the Order (`charge_currency`, `charge_total`, `charge_amount_minor`, `fx_rate_used`; + `OrderCharge.currency`). `order.currency`/`order.total` stay the BASE accounting anchor. Provider support = `ProcessorAccount.supported_currencies` (super-admin dual-list) or the platform default map in `payments/currency_capability.py`. Decimal-aware minor units = `cart/currency.py charge_minor_units` (handles JPY 0-decimal / KWD 3-decimal; replaced all `int(x*100)`).

**Flag is cleared to enable (Safety re-audited SAFE 2026-09-04), but before flipping it on:**
1. Product decision still open: off-session UPSELL charges (`campaigns/upsell_service.py`) stay in BASE, not buyer currency (aggregation-safe via rate=1; charging upsells in buyer currency is deferred).
2. Optional hardening: wrap the (currently unreachable) `orders/service.py charge_rate_to_base` ValueError at the webhook boundary with the durable-flag pattern (`_flag_refund_restore_failure`) to avoid provider-retry 500s.
3. FX inverse: `fx_rate_used = rate_display/rate_store`, so `base = charge_amount / fx_rate_used` (`charge_amount_to_base`). Volume revert + `total_paid_amount` both normalize to base.

Relates to [[project_pipeline_architecture]], [[project_hosting_architecture]].
