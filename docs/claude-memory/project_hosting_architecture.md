---
name: Hosting architecture decision
description: Two separate Contabo VPS — one for Drogon static sites, one for Django e-commerce
type: project
originSessionId: 54c75568-9c18-4af8-89b8-a70366f939e7
---
Two Contabo VPS 10 (€3.60/month each):

- **VPS 1** — IP: `161.97.153.237` — All StaticWebsiteServe (Drogon) deployments — multiple websites, multiple languages
- **VPS 2**: All Django e-commerce sites — fewer pages (< 1 000 to 10 000 max), heavier runtime

**Why:** failure isolation — a bad Django deployment must not take down Drogon sites, and running many websites makes the risk real.

**How to apply:** when building the Django publish path in WebsiteEmpire, target a different SSH host than the Drogon deploy. No code architecture change needed — just a different destination.

## Per-project VPS 1 deployments

| Project | Working dir | Domain | Deployed languages |
|---------|-------------|--------|--------------------|
| Health (biomarky.com) | `/home/cedric/Dropbox/freelancers/projects/workingDirectory/WebsiteEmpire2/Health` | biomarky.com | en:8080, de:8081, fr:8082, ja:8083 |
</content>
</invoke>