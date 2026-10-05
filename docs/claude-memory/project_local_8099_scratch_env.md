---
name: project-local-8099-scratch-env
description: "The local :8099 dev admin runs on scratch_settings with a SEPARATE DB — data ops must target it, not db.sqlite3"
metadata: 
  node_type: memory
  type: project
  originSessionId: f949a749-5e55-46c3-ace7-2b2bad071483
  modified: 2026-09-04T10:24:43.100Z
---

The local dev admin at **http://localhost:8099** (WebsiteEcom/Pradize) runs
`python3 manage.py runserver 127.0.0.1:8099 --noreload` with
`DJANGO_SETTINGS_MODULE=scratch_settings`, where `scratch_settings.py` lives in
`/home/cedric/aspire_rescrape` (on the process's `PYTHONPATH`) and requires
`SCRATCH_TMP=/home/cedric/aspire_rescrape/pradize_import_catalog`.

`scratch_settings` inherits `webecom.settings.development` but REDIRECTS the DBs
to `$SCRATCH_TMP/scratch_default.sqlite3` + `scratch_analytics.sqlite3`,
`MEDIA_ROOT` to `$SCRATCH_TMP/media`, `ROOT_URLCONF=scratch_urls`, and sets
`PRADIZE_URL_SCHEME=http` + `PRADIZE_URL_AUTHORITY_SUFFIX=:8099` + eager Celery.
It is the isolated "aspire import-catalog" demo env.

**Consequence (this bit me 2026-09-04):** any migrate / seed / shell data op that
must affect what the user sees at :8099 MUST run with:
`cd WebsiteEcom && PYTHONPATH=/home/cedric/aspire_rescrape DJANGO_SETTINGS_MODULE=scratch_settings SCRATCH_TMP=/home/cedric/aspire_rescrape/pradize_import_catalog python3 manage.py <cmd>`
NOT the repo default (`db.sqlite3`). Seeding db.sqlite3 leaves :8099 unchanged.

Stores in the scratch DB: id=1 "Vogelvoerkopen", id=2 "Pradize".
Caveat: STILL never run the TEST SUITE with scratch_settings (its demo config
causes false URL-shape failures) — use the repo default for tests. See
[[project_email_templates_i18n]], [[project_buyer_currency_charging]].
