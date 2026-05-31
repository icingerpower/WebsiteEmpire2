# Deploy WebsiteEmpire website to a new VPS

You are deploying a WebsiteEmpire static website (Drogon-based) to a fresh Ubuntu VPS.

## Architecture — read this first

```
/opt/websiteempire/
  StaticWebsiteServe          ← single binary, shared by all languages
  deploy/
    images.db                 ← shared across all languages
    en/
      content.db              ← English pages only
    fr/
      content.db              ← French pages only
    de/
      content.db              ← German pages only
    ja/
      content.db              ← Japanese pages only
    ...
```

One `StaticWebsiteServe` process per language, each:
- Runs with `WorkingDirectory=/opt/websiteempire/deploy/{lang}` — it opens `content.db` from its cwd
- Started with `--port {port} --lang {lang} --images-db /opt/websiteempire/deploy/images.db`
- Serves pages **without** the language prefix in the path (pages are stored as `/index.html`, not `/fr/index.html`)

Nginx strips the language prefix via trailing slash on proxy_pass:
- `location /fr/ { proxy_pass http://127.0.0.1:8081/; }` → `/fr/some-page` becomes `/some-page` at port 8081
- English has no prefix and no trailing slash: `location / { proxy_pass http://127.0.0.1:8080; }`

**Testing a language backend directly** — always use the path WITHOUT the language prefix:
```bash
# CORRECT
curl -s --compressed http://localhost:8081/index.html
# WRONG — will 404 because pages aren't stored with /fr/ prefix
curl -s --compressed http://localhost:8081/fr/index.html
```

**Verify a language serves the right language** — check the lang stored in pages:
```bash
sqlite3 /opt/websiteempire/deploy/fr/content.db 'SELECT lang, COUNT(*) FROM pages GROUP BY lang;'
# Must show only fr rows — if it shows en, the wrong content.db was uploaded
```

---

## Step 1 — Collect information

Ask the user for the following, one message:
- VPS IP address
- Domain name (e.g. biomarky.com)
- Working directory on this machine (the one passed to --workingDir, which contains the deploy/ folder)
- Languages to deploy and their ports (e.g. en:8080, fr:8081)
- Email address for Let's Encrypt SSL

Do NOT ask for the SSH password — the user will type it when SSH/scp prompts for it.

> **SSH note**: You cannot type SSH passwords interactively from this tool. Ask the user to
> run any `ssh`/`scp`/`rsync` commands themselves by prefixing them with `!` in the prompt
> (e.g. `! ssh root@IP "..."`), or suggest they set up SSH key auth first:
> `ssh-copy-id root@{IP}` — after that all commands run without a password prompt.

> ⚠️ **SUBSTITUTION RULE — MANDATORY**: Every command you give the user must have
> **all placeholders replaced with real values** collected in this step. Never show a command
> with `{IP}`, `{domain}`, `{lang}`, `{port}`, etc. still in it. If the user sees a literal
> `{IP}` in a command they have to run, that is your mistake. Always substitute before
> presenting the command.

> ⚠️ **FILE CREATION RULE — MANDATORY**: Never use `cat > file << 'EOF'` heredocs for
> creating config files — copy-paste of heredocs always fails in the terminal. Instead:
> 1. Use the **Write tool** to create the file locally at `/tmp/` (you have filesystem access)
> 2. Give the user a single `scp` or `rsync` command to upload it
>
> This applies to: nginx configs, systemd service files, and any other multi-line file.

---

## Step 2 — Prepare VPS

```bash
! ssh root@{IP} "apt update && apt upgrade -y && apt install -y libjsoncpp25 nginx certbot python3-certbot-nginx ufw sqlite3 && ufw allow OpenSSH && ufw allow 'Nginx Full' && ufw --force enable && mkdir -p /opt/websiteempire/deploy"
```

## Step 3 — Upload binary

```bash
! scp /home/cedric/Applications/WebsiteEmpire2/build/StaticWebsiteServe/StaticWebsiteServe root@{IP}:/opt/websiteempire/
! ssh root@{IP} "chmod +x /opt/websiteempire/StaticWebsiteServe"
```

## Step 4 — Upload deploy directory

Upload the whole deploy folder (images.db + all language subdirs):
```bash
! rsync -avz --progress {workingDir}/deploy/ root@{IP}:/opt/websiteempire/deploy/
```

**After upload, verify each language content.db contains the right language:**
```bash
! ssh root@{IP} "for lang in en fr de; do echo \"=== \$lang ===\"; sqlite3 /opt/websiteempire/deploy/\$lang/content.db 'SELECT lang, COUNT(*) FROM pages GROUP BY lang;'; done"
```
Each directory must show only its own lang code. If a directory shows `en` when it should show `fr`, the wrong file was uploaded — stop and re-rsync that language subdirectory only.

**Clean any stale WAL/SHM files before starting services:**
```bash
! ssh root@{IP} "find /opt/websiteempire/deploy -name '*.db-wal' -o -name '*.db-shm' | xargs rm -f"
```

## Step 5 — Test binary on VPS

```bash
! ssh root@{IP} "cd /opt/websiteempire/deploy/{firstLang} && /opt/websiteempire/StaticWebsiteServe --port {firstPort} --lang {firstLang} --images-db /opt/websiteempire/deploy/images.db &"
```

Wait 2 seconds, then verify (path WITHOUT language prefix):
```bash
! ssh root@{IP} "curl -s --compressed http://localhost:{firstPort}/index.html | head -3"
```

If you see HTML, continue. If you see an error or 404, stop and diagnose.

## Step 6 — Configure nginx

> ⚠️ **CRITICAL — nginx config and SSL order of operations**
>
> certbot modifies the nginx config file **in-place**. If you overwrite it after certbot runs,
> SSL stops working. Never skip the certbot --reinstall step after updating the config.
>
> **Correct order:**
> 1. Write the HTTP-only config (Write tool → scp)
> 2. Run certbot — it patches SSL in
> 3. Never touch the config again without re-running certbot --reinstall
>
> **Adding a language later:**
> - Write new HTTP-only config (Write tool → scp)
> - Re-run: `certbot --nginx -d {domain} -d www.{domain} --reinstall`

Use the **Write tool** to create `/tmp/{domain}.nginx` (HTTP only — no SSL):

```nginx
server {
    server_name {domain} www.{domain};

    location = / {
        if ($http_accept_language ~* "^{lang2}") {
            return 302 /{lang2}/index.html;
        }
        if ($http_accept_language ~* "^{lang3}") {
            return 302 /{lang3}/index.html;
        }
        return 302 /index.html;
    }

    location /{lang2}/ {
        proxy_pass http://127.0.0.1:{port2}/;
        proxy_set_header Host $host;
    }

    location /{lang3}/ {
        proxy_pass http://127.0.0.1:{port3}/;
        proxy_set_header Host $host;
    }

    location / {
        proxy_pass http://127.0.0.1:{port1};
        proxy_set_header Host $host;
    }
}
```

Rules:
- Non-English languages: `proxy_pass` URL **must have trailing slash** (strips the language prefix)
- English catch-all: **no trailing slash** on proxy_pass, always last
- Use 302 (not 301) for root redirect so Google crawls all language versions

Upload and enable:
```bash
! scp /tmp/{domain}.nginx root@{IP}:/etc/nginx/sites-available/{domain}
! ssh root@{IP} "ln -sf /etc/nginx/sites-available/{domain} /etc/nginx/sites-enabled/{domain} && nginx -t && systemctl reload nginx"
```

## Step 7 — SSL via Let's Encrypt

```bash
! ssh root@{IP} "certbot --nginx -d {domain} -d www.{domain} --non-interactive --agree-tos -m {userEmail}"
```

## Step 8 — systemd services (one per language)

Use the **Write tool** to create one `/tmp/website-{lang}.service` file per language, then upload all at once.

Service file template:
```ini
[Unit]
Description=WebsiteEmpire {LANG_UPPER}
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/websiteempire/deploy/{lang}
ExecStart=/opt/websiteempire/StaticWebsiteServe --port {port} --lang {lang} --images-db /opt/websiteempire/deploy/images.db
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
```

Upload all service files and start:
```bash
! rsync /tmp/website-{lang1}.service /tmp/website-{lang2}.service root@{IP}:/etc/systemd/system/
! ssh root@{IP} "systemctl daemon-reload && systemctl enable --now website-{lang1}.service website-{lang2}.service && systemctl status website-{lang1}.service website-{lang2}.service --no-pager"
```

Verify each language (path WITHOUT language prefix):
```bash
! ssh root@{IP} "curl -s --compressed http://localhost:{port1}/index.html | head -3"
! ssh root@{IP} "curl -s --compressed http://localhost:{port2}/index.html | head -3"
```

## Step 9 — Final check

```bash
! ssh root@{IP} "curl -sI https://{domain}/ | grep -i location"
! ssh root@{IP} "curl -s --compressed https://{domain}/index.html | head -3"
! ssh root@{IP} "curl -s --compressed https://{domain}/{lang2}/index.html | head -3"
```

If all return expected HTML, the site is live at https://{domain}.

Remind the user to submit `https://{domain}/sitemap.xml` to Google Search Console.

---

## Adding a new language to an existing VPS

1. **Upload only the new language subdirectory** (never rsync the whole deploy/ — it may disrupt running services):
```bash
! rsync -avz --progress {workingDir}/deploy/{newLang}/ root@{IP}:/opt/websiteempire/deploy/{newLang}/
```

2. **Verify content is correct:**
```bash
! ssh root@{IP} "sqlite3 /opt/websiteempire/deploy/{newLang}/content.db 'SELECT lang, COUNT(*) FROM pages GROUP BY lang;'"
```

3. **Clean stale WAL/SHM:**
```bash
! ssh root@{IP} "rm -f /opt/websiteempire/deploy/{newLang}/content.db-wal /opt/websiteempire/deploy/{newLang}/content.db-shm"
```

4. **Write and upload the systemd service** (Write tool → scp).

5. **Enable and start:**
```bash
! ssh root@{IP} "systemctl daemon-reload && systemctl enable --now website-{newLang}.service"
```

6. **Test the backend directly** (path WITHOUT language prefix):
```bash
! ssh root@{IP} "curl -s --compressed http://localhost:{newPort}/index.html | head -3"
```

7. **Update nginx** (Write tool → scp the new HTTP-only config, then certbot --reinstall):
```bash
! scp /tmp/{domain}.nginx root@{IP}:/etc/nginx/sites-available/{domain}
! ssh root@{IP} "nginx -t && systemctl reload nginx && certbot --nginx -d {domain} -d www.{domain} --reinstall"
```

---

## Redeploying after a republish (content update only)

```bash
# 1. Rsync each updated language dir individually to avoid disrupting other languages
! rsync -avz --progress {workingDir}/deploy/{lang}/ root@{IP}:/opt/websiteempire/deploy/{lang}/

# 2. Restart the affected service(s)
! ssh root@{IP} "systemctl restart website-{lang}.service"
```

Do NOT touch the nginx config during a content redeploy.

---

## Notes
- Always use `curl --compressed` — Drogon serves gzip by default
- SSL auto-renews every 90 days — no action needed
- Use `sqlite3 content.db 'SELECT lang, COUNT(*) FROM pages GROUP BY lang;'` to verify a content.db has the right language before starting a service on it
