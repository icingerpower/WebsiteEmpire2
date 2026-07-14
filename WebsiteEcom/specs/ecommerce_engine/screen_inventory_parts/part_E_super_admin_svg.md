# Part E — Super-admin diagrams: page map, organizations, processors, rules, payment methods

Source: 5 SVG design diagrams in `/home/cedric/Dropbox/Applications/WebsiteEmpire2/spec-ecom/`.
All five diagrams share a common application chrome (described once here, valid for every screen):

- **Top bar (dark):** app logo (play-triangle in a circle) + product name **"Pradize Control Plane"**; a global search field with placeholder **"Search organizations, processors, rules…"**; an environment label **"Production"**; a primary **"Publish"** button; a user avatar chip **"CB"**.
- **Left sidebar (dark), 8 menu entries in order:** Overview, Organizations, Processor Accounts, Organization Rules, Payment Methods, Simulation, Decision Logs, Settings. Footer caption: **"Super-admin / shared across stores"**. The active entry is highlighted with a filled pill (Overview on the page map; Organizations / Processor Accounts / Organization Rules / Payment Methods on their respective screens).
- Chrome implications: environment switching (at least a Production indicator), a publish/deploy step for configuration ("Publish" button), global cross-entity search, and an authenticated super-admin user.

---

### Super Admin — Information Architecture / Page Map (`super-admin-00-page-map.svg`)

**Purpose:** Defines the overall page map of the super-admin "Payment Control Plane" — the minimal shared surface for payment governance across multiple ecommerce websites — and the recommended setup and runtime ordering.

**Content:**

- Page title: **"Payment Control Plane — Page Map"**. Subtitle: *"Minimal shared super-admin surface for payment governance across multiple ecommerce websites."*
- Active sidebar item: **Overview** (i.e., this page map is what the Overview page documents/represents).
- Full menu hierarchy (sidebar):
  1. Overview
  2. Organizations
  3. Processor Accounts
  4. Organization Rules
  5. Payment Methods
  6. Simulation
  7. Decision Logs
  8. Settings
- Main canvas: a large white panel containing **four numbered cards connected left-to-right by arrows** (1 → 2 → 3 → 4), expressing both setup order and dependency direction:
  - **Card "1. Organizations"** — caption: *"Define legal merchant entities. One default fallback entity."* Bulleted items:
    - Default org
    - Country coverage
    - Thresholds
    - Eligibility
    - Descriptors
  - → arrow →
  - **Card "2. Processor Accounts"** — caption: *"Create accounts only after an organization exists."* Bulleted items:
    - Belongs to org
    - Card / PayPal / Crypto
    - Credentials
    - Health status
    - Shared across stores
  - → arrow →
  - **Card "3. Organization Rules"** — caption: *"Country / area first, then split or threshold."* Bulleted items:
    - Stage 1 geography
    - Stage 2 split %
    - Stage 2 threshold switch
    - Default fallback
  - → arrow →
  - **Card "4. Payment Methods"** — caption: *"What buyer sees and how processor options behave."* Bulleted items:
    - Enable Card / PayPal / Crypto
    - Alternate vs backup
    - Easy reorder
    - Hide if no route
- Footer annotation (bold): *"Recommended runtime order: enabled method → geography rule → split/threshold rule → processor option strategy (alternate or backup)."*
- Footer annotation (secondary): *"Stores should only assign profiles and enable/disable methods locally. Secrets and shared routing stay in super admin."*

**Implied features:**
- A super-admin control plane shared by all stores (multi-tenant), distinct from per-store admin.
- Strict creation dependency chain: Organization must exist before Processor Accounts; rules and payment methods build on both.
- A runtime payment-routing decision pipeline in the exact order: (1) is the method enabled → (2) geography rule → (3) split/threshold rule → (4) processor option strategy (alternate or backup).
- Per-store capabilities limited to: assigning profiles and enabling/disabling methods locally. Secrets and routing config are super-admin-only (security boundary).
- Three additional pages exist in the menu but have no dedicated diagram: **Overview** (dashboard), **Simulation** (test a routing decision without a real order), **Decision Logs** (audit of routing decisions), **Settings**.
- A "Publish" workflow implies config is staged then published (draft vs live configuration).

**Uncertainties:**
- CRITICAL — **Simulation, Decision Logs, Overview, Settings pages have no diagrams**: content, data model, and retention of decision logs (which record money-routing decisions and may contain personal data) are unspecified.
- CRITICAL — **"Publish" semantics unspecified**: is configuration versioned/staged and atomically published to stores, or applied live on save? Affects data model (draft vs published state) and rollout safety of routing changes.
- MEDIUM — "Stores should only assign profiles" — the notion of a store-level "profile" (a bundle of payment config assigned to a store?) is mentioned only here and never defined.
- LOW — "Production" environment label suggests multiple environments (e.g., test/production); safe default: one production environment plus processor test credentials.

---

### Super Admin — Organizations (`super-admin-01-organizations.svg`)

**Purpose:** Manage the legal entities that can act as merchant of record, including the single default fallback organization, per-organization country coverage, monthly thresholds, method eligibility, and descriptors.

**Content:**

- Page title: **"Organizations"**. Subtitle: *"Define the legal entities that can act as merchant of record. One entity is the default fallback when no rule overrides it."*
- **Filter bar:**
  - Search field: "Search organizations…"
  - "Country / Area" dropdown (shown value: "All")
  - "Store group" dropdown (shown value: "All stores")
  - "Status" dropdown (shown value: "Active")
  - Warning badge (amber): **"Default organization required"**
  - Primary button: **"Add org"**
- **Organization list panel** — caption: *"Rules evaluate country / area first, then split or threshold logic. If nothing matches, the default organization is used."* Table columns: **Priority | Default | Organization | Country / area | Status | Current month | Threshold**. Rows (sample data):
  | Priority | Default | Organization | Country / area | Status | Current month | Threshold |
  |---|---|---|---|---|---|---|
  | 1 | YES (green badge) | Cedric SASU / FR | EU, CH, UK | Active | €84,200 | €100,000 |
  | 2 | — | Lin Trading Ltd / HK | CN, HK, SG, ROW | Active | €51,400 | — |
  | 3 | — | Sister Commerce EI / FR | FR, BE, ES, IT | Active | €17,650 | €40,000 |
  | 4 | — | Backup LLC / US | US, CA, MX | Draft | $0 | $50,000 |
  - Note under the table: *"Rules order can be changed. Default entity is used only after geography and allocation rules fail."*
- **Create / edit organization panel** (right side) — caption: *"This object owns tax profile, descriptors, caps, and attached processors."* Fields:
  - **Display name** (text, e.g. "Cedric SASU / FR")
  - **Legal name** (text, e.g. "Pradize SASU")
  - Checkbox (checked): **"Default organization if no rule matches"**
  - **Registration country** (select, e.g. "France")
  - **Settlement currencies** (multi-value, e.g. "EUR, USD")
  - **Buyer country / area coverage** (multi-value, e.g. "EU, CH, UK")
  - **Monthly threshold** (number, e.g. "100000") + **Threshold currency** (select, e.g. "EUR")
  - **Support / statement descriptor** (text, e.g. "PRADIZE FR")
  - Eligibility checkboxes: **"Eligible for card payments"** (checked), **"Eligible for PayPal payments"** (checked), **"Eligible for crypto payments"** (unchecked)
  - Buttons: **Cancel**, **Save org** (primary), **Open processors** (dark — navigates to that org's processor accounts)

**Implied features:**
- Organization entity with: display name, legal name, default flag (exactly one org must be default — "Default organization required" warning enforces existence), registration country, settlement currencies (multiple), buyer country/area coverage (list of countries and areas like EU/ROW), monthly threshold amount + threshold currency, statement descriptor, per-method-family eligibility flags (card / PayPal / crypto), status (Active / Draft at minimum).
- Live tracking of **current-month processed volume per organization** (Current month column) compared against the monthly threshold — requires aggregating order/payment amounts per org per calendar month, in the threshold currency.
- Organizations are orderable (Priority column, "Rules order can be changed").
- Filtering by country/area, store group, and status; free-text search.
- Cross-navigation from an organization to its processor accounts ("Open processors").
- Area aliases exist as first-class values: EU, ROW ("rest of world"), plus ISO country codes.

**Uncertainties:**
- CRITICAL — **Meaning of the org-level Priority column vs Stage-1 geography rules**: the Organizations table shows priorities and coverage, but routing is defined in Organization Rules. Is the org list order actually used at runtime (e.g., as a tiebreaker or implicit geography), or is Priority here purely informational/duplicated display of rule order? Duplicated routing sources would be a money-impacting ambiguity.
- CRITICAL — **Threshold semantics**: what happens when Current month reaches Threshold at the organization level (blocked? excluded from candidates? handed to fallback?) vs the Stage-2 "threshold switch" rule. Also: which amount counts (authorized, captured, net of refunds?) and month boundary/timezone. Direct money/compliance impact.
- CRITICAL — **Currency conversion for thresholds**: Current month shows € for EU orgs and $ for the US org; orders may arrive in other currencies. Conversion rules (rate source, timing) for threshold accounting are unspecified.
- MEDIUM — "This object owns tax profile" is stated in the caption but **no tax profile fields** appear in the form — either a missing form section or a future feature.
- MEDIUM — "Store group" filter implies stores are grouped, but store-group management is not shown anywhere.
- MEDIUM — Org with Threshold "—" (Lin Trading): does absence of a threshold mean unlimited, or inherited default?
- LOW — Status vocabulary: only "Active" and "Draft" are shown; assume at least these two plus a way to deactivate.
- LOW — "Settlement currencies" plural vs processor "Settlement currency" singular — assume org supports a set, each processor account uses one.

---

### Super Admin — Processor Accounts (`super-admin-02-processor-accounts.svg`)

**Purpose:** Manage payment processor accounts (Stripe, HiPay, Mollie, PayPal, BTCPay, BitPay…), each belonging to exactly one organization and one shopper-visible method family, with credentials, health status, country/currency support, and primary/backup role flags.

**Content:**

- Page title: **"Processor Accounts"**. Subtitle: *"Create payment processor accounts after an organization exists. Each account belongs to one organization and one shopper-visible method family."*
- **Filter bar:**
  - "Organization" dropdown (shown: "Cedric SASU / FR")
  - "Method family" dropdown (shown: "Card")
  - "Processor type" dropdown (shown: "All processors")
  - "Status" dropdown (shown: "Active")
  - Checkbox (checked): **"Show backup accounts"**
  - Primary button: **"Add account"**
- **Processor accounts list panel** — caption: *"Processors are grouped by shopper-visible family: card, PayPal, crypto."* Family tabs/badges: **Card** (blue), **PayPal** (purple), **Crypto** (black). Table columns: **Priority | Processor account | Org | Method | Strategy | Health**. Rows (priority restarts per method family):
  | Priority | Processor account | Org | Method | Strategy | Health |
  |---|---|---|---|---|---|
  | 1 | Stripe FR Main | Cedric SASU / FR | Card | Primary | Healthy (green) |
  | 2 | HiPay FR Backup | Cedric SASU / FR | Card | Backup | Ready (blue) |
  | 3 | Mollie Sister FR | Sister Commerce EI / FR | Card | Backup | Ready (blue) |
  | 1 | PayPal Cedric | Cedric SASU / FR | PayPal | Primary | Healthy (green) |
  | 1 | BTCPay Main | Lin Trading Ltd / HK | Crypto | Primary | Healthy (green) |
  | 2 | BitPay Backup | Lin Trading Ltd / HK | Crypto | Backup | Ready (blue) |
  - Note under the table: *"Drag priority inside each method family. 'Backup' order matters if the primary is blocked or disabled."*
- **Create / edit processor account panel** — caption: *"Credentials and technical configuration are shared across all assigned stores via the control plane."* Fields:
  - **Organization** (select, e.g. "Cedric SASU / FR")
  - **Account label** (text, e.g. "Stripe FR Main")
  - **Processor type** (select, e.g. "Stripe")
  - **Method family** (select, e.g. "Card")
  - **Supports buyer countries** (multi-value, e.g. "EU, CH, UK")
  - **Settlement currency** (select, e.g. "EUR")
  - **Runtime status** (read-only style field, e.g. "Healthy / Active")
  - **Secret / credential reference** (text, e.g. `vault://payments/stripe-fr-main`)
  - Checkboxes: **"May be used as primary"** (checked), **"May be used as backup only"** (unchecked), **"Block new traffic manually"** (unchecked), **"Shared across multiple stores"** (checked)
  - Buttons: **Test creds**, **Cancel**, **Save account** (primary)

**Implied features:**
- ProcessorAccount entity: label, FK organization (exactly one), processor type (Stripe, HiPay, Mollie, PayPal, BTCPay, BitPay — an extensible catalog), method family (Card / PayPal / Crypto), supported buyer countries/areas, settlement currency (single), credential stored as an **external vault reference** (secrets never stored inline — `vault://` URI scheme), runtime health status, role capability flags (may-be-primary / backup-only), a manual kill-switch ("Block new traffic manually"), multi-store sharing flag.
- Health-state machine with at least: **Healthy** (active primary, processing), **Ready** (configured, standing by as backup); manual blocking is a separate flag. Implies automated health monitoring.
- Per-family drag-and-drop priority ordering; backup ordering determines failover sequence when primary is blocked/disabled.
- Credential verification action ("Test creds") that calls the processor's API with the vault-referenced credentials.
- Filtering by organization, method family, processor type, status; option to hide backup accounts from the list.

**Uncertainties:**
- CRITICAL — **Vault backend unspecified**: `vault://payments/...` implies an external secret manager (HashiCorp Vault or similar). Choice, access control, and rotation policy are security-critical and undefined.
- CRITICAL — **Health status source**: who sets Healthy/Ready — automated probes, webhook error rates, or manual? What triggers automatic failover to backups (health flips vs only manual block)? Directly affects payment success and money flow.
- MEDIUM — Relationship between this screen's per-family Priority and the per-method processor-option ordering on the Payment Methods screen (04): both show orderable primary/backup lists of the same accounts. One shared ordering, or two (global vs per-method) orderings? 2 clear options.
- MEDIUM — "May be used as primary" and "May be used as backup only" as two separate checkboxes can be combined inconsistently (both unchecked = unusable account?). Likely should be a single role enum; needs a validation rule.
- MEDIUM — Status filter ("Active") vs Runtime status ("Healthy / Active"): is there a separate administrative status (active/disabled/draft) distinct from health? Field shown as one combined string.
- LOW — "Show backup accounts" default state; assume shown by default in edit contexts.
- LOW — Processor type list is open-ended; assume a pluggable adapter per processor type.

---

### Super Admin — Organization Rules (`super-admin-03-organization-rules.svg`)

**Purpose:** Define the two-stage routing decision tree that selects the merchant organization for an order: Stage 1 geography (country/area) picks an organization or candidate pool; Stage 2 applies, per pool, either a percentage income split or a threshold switch; a default organization is the explicit final fallback.

**Content:**

- Page title: **"Organization Rules"**. Subtitle: *"Two-stage logic: 1) choose organization by country / area first, 2) inside that pool apply either income split (%) or threshold switch."*
- **Top toolbar:** three tab-style buttons — **"1. Country / area rules"** (selected), **"2. Split / threshold rules"**, **"Rule precedence"** — plus a green status badge **"Default org: Cedric SASU / FR"** and a primary **"Add rule"** button.
- **Stage 1 panel — "Stage 1 — Geography chooses the candidate organization set"** — caption: *"Country or area rules are evaluated first. The first matching rule wins unless the rule explicitly returns multiple candidate organizations."* Table columns: **Order | Rule name | Match | Result | Status**. Rows:
  | Order | Rule name | Match | Result | Status |
  |---|---|---|---|---|
  | 1 | EU default | Country in EU | Cedric SASU / FR | Active |
  | 2 | China / HK / SG | Country in CN, HK, SG | Lin Trading Ltd / HK | Active |
  | 3 | France + sister candidate | Country in FR | Cedric + Sister pool | Active |
  | 4 | North America | Country in US, CA, MX | Backup LLC / US | Draft |
  - Note under the table: *"Example: a France order can return a pool of 2 organizations, then stage 2 decides split or threshold switch."*
  - (Note: rule 3 "Country in FR" can only fire because it is ordered relative to rule 1 "Country in EU" — first-match ordering is significant. As drawn, order 1 would match FR first; see uncertainties.)
- **Stage 1 — Rule editor panel** — caption: *"Country / area selector decides which organization or organization pool is eligible."* Fields:
  - **Rule name** (text, e.g. "France + sister candidate")
  - **Priority** (number, e.g. "3")
  - **When buyer country / area is** (selector, e.g. "France")
  - Radio group (result cardinality): **"Return one organization"** (unselected) / **"Return an organization pool for stage 2"** (selected)
  - **Returned organization(s)** (multi-select, e.g. "Cedric SASU / FR, Sister Commerce EI / FR")
- **Stage 2 — Split / threshold editor panel** — caption: *"Applied only after geography. Choose one mode per returned pool."* Fields:
  - **Applies to geography result** (select referencing a Stage-1 rule, e.g. "France + sister candidate")
  - Radio group (exactly one mode per pool):
    - **"Break down income by percentage"** (selected) — with sub-field **"Split example"**: "Cedric 50% / Sister 50%"
    - **"Switch organization once threshold is reached"** (unselected) — with sub-field **"Threshold example"**: "Use Cedric until €100,000, then Sister"
  - Buttons: **Cancel**, **Save rule** (primary)

**Implied features:**
- A **decision tree with explicit fallbacks, not a flat priority list**:
  1. Evaluate Stage-1 geography rules in order; **first match wins**.
  2. A Stage-1 rule returns either exactly one organization (routing done, subject to eligibility) or a **pool of candidate organizations**.
  3. If a pool is returned, exactly one Stage-2 rule bound to that geography rule applies **either** a percentage split (rotate/allocate income between pool members by %) **or** a threshold switch (use org A until its cumulative amount reaches a threshold, then switch to org B).
  4. If no geography rule matches (or downstream allocation fails), the **default organization** is used — visible as a permanent badge, matching the Organizations screen's "Default entity is used only after geography and allocation rules fail."
- Rule entities: GeographyRule {name, priority/order, country-or-area match set, result mode (single|pool), returned org(s), status Active/Draft}; AllocationRule {geography-rule FK, mode (percentage_split | threshold_switch), split percentages per org, or ordered org sequence + threshold amount(s)}.
- A "Rule precedence" view (third tab, content not drawn) to inspect overall evaluation order.
- Country matching supports both areas (EU) and country lists (CN, HK, SG); rules have Active/Draft status so drafts don't affect runtime.

**Uncertainties:**
- CRITICAL — **Overlapping geography rules ordering paradox**: as drawn, "Country in EU" at order 1 would always match France before rule 3 "Country in FR" at order 3. Either specific-country rules must outrank area rules, or the sample ordering is illustrative-only. First-match semantics with overlapping sets must be pinned down — it changes which legal entity receives money.
- CRITICAL — **Percentage split mechanics**: "Break down income by percentage" — is the split applied per-order (probabilistic/round-robin assignment of whole orders to reach 50/50 over volume) or by amount accounting? An order cannot be half-charged to two merchants, so the allocation algorithm (weighted rotation? amount-based rebalancing?) is a money-critical unknown.
- CRITICAL — **Threshold switch accounting**: which counter drives "until €100,000" — the org's global monthly threshold from the Organizations screen, or a per-rule counter? Reset period (monthly?), currency conversion, and behavior when the *second* org also hits a cap (fall to default? third org?) are unspecified.
- CRITICAL — **Interaction with org eligibility/coverage**: if a geography rule returns an org that is not eligible for the chosen payment method (e.g., crypto unchecked) or whose coverage excludes the buyer country, is it skipped within the pool, does the rule fail to default, or is the config rejected at save time?
- MEDIUM — "The first matching rule wins **unless** the rule explicitly returns multiple candidate organizations" — ambiguous phrasing: does a pool-returning match also stop evaluation (pool replaces single result), or does evaluation continue collecting? Most plausible reading: match always stops evaluation; the "unless" only contrasts single vs pool result. 2 interpretations.
- MEDIUM — "Rule precedence" tab and "2. Split / threshold rules" tab contents are not drawn; assumed to be a read-only ordered listing and a Stage-2 rule list respectively.
- MEDIUM — Can multiple Stage-2 rules target the same geography pool, or exactly one ("Choose one mode per returned pool" suggests exactly one)? Enforcement (unique constraint) needed.
- LOW — Pool size shown is 2; assume N-org pools with N-way splits / ordered threshold chains are allowed.

---

### Super Admin — Payment Methods (`super-admin-04-payment-methods.svg`)

**Purpose:** Configure the three shopper-visible payment methods (Card, PayPal, Crypto) — checkout enablement, displayed label, the ordered list of processor options behind each method, and the per-method option strategy (backup chain vs alternate evenly).

**Content:**

- Page title: **"Payment Methods"**. Subtitle: *"Shopper sees Card / PayPal / Crypto. System chooses the specific processor account and organization behind each method."*
- **Info banner:** *"If a method is enabled it is displayed to the buyer. Processor choice happens behind the scenes."* Two badges: **"No country rules here"** (blue) and **"Reorder by drag handle"** (grey).
- **Three method cards side by side** (Card, PayPal, Crypto), each with the caption *"Shopper-visible method. All enabled methods are displayed."* and the same field skeleton:
  - Checkbox **"Enabled at checkout"** (checked on all three cards)
  - **Displayed label** (text: "Card" / "PayPal" / "Crypto")
  - Section **"Processor options"**
  - Common footer checkboxes: **"Hide method when no processor route is available"** (checked on all three) and **"Allow stores to locally disable this method"** (checked on all three)
  - Buttons per card: **Preview**, **Save** (primary)
- **Card card specifics:**
  - Strategy radio: **"Use as backup chain"** (unselected) / **"Alternate evenly"** (selected)
  - Draggable processor option list (☰ handles): 1. **Stripe FR Main** — Cedric SASU / FR — badge **Primary**; 2. **HiPay FR Backup** — Cedric SASU / FR — badge **Backup**; 3. **Mollie Sister FR** — Sister Commerce EI / FR — badge **Backup**
  - **Behavior** explainer: *"Alternate evenly: rotate traffic between listed processor options."* / *"If one option is blocked, continue with the next available one."*
- **PayPal card specifics:**
  - Single processor option: **☰ PayPal Cedric** — badge **Primary**; note *"Single option → no alternation rules needed"* (no strategy radio shown for a single option)
  - Extra checkbox (checked): **"Show when enabled even if only one route exists"**
- **Crypto card specifics:**
  - Strategy radio: **"Use as backup chain"** (selected) / **"Alternate evenly"** (unselected)
  - Draggable list: 1. **BTCPay Main** — Lin Trading Ltd / HK — badge **Primary**; 2. **BitPay Backup** — Lin Trading Ltd / HK — badge **Backup**
  - **Behavior** explainer: *"Backup chain: first item is used. Others are only used if the first is unavailable."* / *"Reordering the list changes both primary and backup priority."*

**Implied features:**
- PaymentMethod entity (fixed set of three families: Card, PayPal, Crypto): enabled flag, displayed label (customizable, presumably translatable), ordered list of processor options (references to ProcessorAccounts of the same family, potentially across different organizations), strategy enum (**backup_chain** | **alternate_evenly**), hide-when-no-route flag, allow-store-local-disable flag, PayPal-style show-even-if-single-route flag.
- Two runtime option strategies:
  - **Backup chain:** always use first option; subsequent options only on unavailability.
  - **Alternate evenly:** rotate traffic across options; skip blocked options and continue with next available (graceful degradation in both modes).
- Explicitly **no geography logic at this layer** ("No country rules here") — geography lives exclusively in Organization Rules; this screen only sequences processor options.
- Checkout visibility rules: enabled methods are displayed; optionally hidden when no processor route resolves for the buyer (requires a pre-checkout route resolution check).
- Store-level override permission: stores may disable a method locally only if super-admin allows it.
- "Preview" action (presumably renders the buyer-facing checkout method list).

**Uncertainties:**
- CRITICAL — **Layer-ordering conflict between org routing and method options**: the page map says runtime order is method → geography → split/threshold → option strategy, yet the Card method's option list mixes organizations (Cedric + Sister). If geography/split rules already chose an organization, are method options filtered to that org's accounts (and the strategy applied within the remainder), or can the option strategy override the chosen org? This decides which legal entity takes the money and must be resolved.
- CRITICAL — **"Alternate evenly" vs Stage-2 "split %"**: two separate traffic-distribution mechanisms exist (org-level income split, method-level even alternation). Their composition (does 50/50 org split combine with even processor rotation multiplicatively?) is undefined and money-impacting.
- MEDIUM — Primary/Backup badges here vs the Strategy column on Processor Accounts (02): is the badge derived from list position (first = Primary) as *"Reordering the list changes both primary and backup priority"* suggests, or from the account's own may-be-primary/backup-only flags? Interaction with "backup only" accounts placed first needs a rule.
- MEDIUM — "Alternate evenly" rotation scope: per store, per method globally, or per buyer session? And is "evenly" strict round-robin or randomized? 2-3 plausible options.
- MEDIUM — Method set extensibility: exactly three families are drawn everywhere; assume the family list is fixed (Card/PayPal/Crypto) for v1, but adding a family (e.g., bank transfer) would touch orgs (eligibility flags), accounts, and this screen.
- LOW — "Preview" behavior; safe default: read-only rendering of the checkout method selector.
- LOW — Displayed label localization; safe default: single label string per method for v1 (stores/sites may translate).

---

## Cross-diagram summary of the runtime decision pipeline (as specified)

1. **Payment Methods:** method must be "Enabled at checkout" (and have a resolvable route if "hide when no route" is set).
2. **Organization Rules — Stage 1 (geography):** first matching country/area rule returns one org or an org pool.
3. **Organization Rules — Stage 2 (allocation):** for a pool, exactly one mode — percentage split OR threshold switch.
4. **Fallback:** default organization if geography and allocation both fail to produce an org.
5. **Payment Methods — option strategy:** within the method, pick processor account by backup chain or even alternation, skipping blocked/unhealthy accounts (health and manual block from Processor Accounts).
