# Deploy WebsiteEmpire website to a new VPS

You are deploying a WebsiteEmpire static website (Drogon-based) to a fresh Ubuntu VPS.

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

## Step 2 — Prepare VPS

Ask the user to run:

```bash
ssh root@{IP} "apt update && apt upgrade -y && apt install -y libjsoncpp25 nginx certbot python3-certbot-nginx ufw sqlite3 && ufw allow OpenSSH && ufw allow 'Nginx Full' && ufw --force enable && mkdir -p /opt/websiteempire/deploy"
```

## Step 3 — Upload binary

```bash
scp /home/cedric/Applications/WebsiteEmpire2/build/StaticWebsiteServe/StaticWebsiteServe root@{IP}:/opt/websiteempire/
```

## Step 4 — Upload deploy directory

```bash
rsync -avz --progress {workingDir}/deploy/ root@{IP}:/opt/websiteempire/deploy/
```

## Step 5 — Test binary on VPS

```bash
ssh root@{IP} "chmod +x /opt/websiteempire/StaticWebsiteServe && cd /opt/websiteempire/deploy/{firstLang} && /opt/websiteempire/StaticWebsiteServe --port {firstPort} --lang {firstLang} --images-db /opt/websiteempire/deploy/images.db &"
```

Wait 2 seconds, then verify:
```bash
ssh root@{IP} "curl -s --compressed http://localhost:{firstPort}/ | head -3"
```

If you see HTML, continue. If you see an error, stop and diagnose.

## Step 6 — Configure nginx

> ⚠️ **CRITICAL — nginx config and SSL order of operations**
>
> certbot modifies the nginx config file **in-place**, adding SSL `listen 443` directives,
> certificate paths, and a redirect from port 80. If you overwrite or rsync the nginx config
> AFTER certbot has run, those SSL additions are wiped and HTTPS stops working.
>
> **Correct order (enforced below):**
> 1. Write the nginx config (HTTP only)
> 2. Run certbot — it patches the config with SSL
> 3. Never touch the nginx config file again after certbot
>
> **If you must update the nginx config later** (e.g. adding a language):
> - Write the new HTTP-only config
> - Re-run: `certbot --nginx -d {domain} -d www.{domain}` and choose **option 1 "Reinstall existing certificate"**
> - This re-patches the config with SSL without issuing a new certificate

Write the nginx config locally, then rsync it to avoid heredoc quoting issues:

```bash
# Write locally first
cat > /tmp/{domain}.nginx << 'EOF'
server {
    server_name {domain} www.{domain};

    {locationBlocks}
}
EOF

# Upload
rsync /tmp/{domain}.nginx root@{IP}:/etc/nginx/sites-available/{domain}

# Symlink and reload
ssh root@{IP} "ln -sf /etc/nginx/sites-available/{domain} /etc/nginx/sites-enabled/{domain} && nginx -t && systemctl reload nginx"
```

Build `{locationBlocks}` from the languages list. Always include:
1. A `location = /` block that auto-detects browser language and redirects with 302
2. A `location /langcode/` block for each non-English language
3. A catch-all `location /` for English (always last)

Example for en+fr+de:

```
location = / {
    if ($http_accept_language ~* "^fr") {
        return 302 /fr/index.html;
    }
    if ($http_accept_language ~* "^de") {
        return 302 /de/index.html;
    }
    return 302 /index.html;
}
location /fr/ {
    proxy_pass http://127.0.0.1:8081;
    proxy_set_header Host $host;
}
location /de/ {
    proxy_pass http://127.0.0.1:8082;
    proxy_set_header Host $host;
}
location / {
    proxy_pass http://127.0.0.1:8080;
    proxy_set_header Host $host;
}
```

For 2 languages (en+fr) drop the `de` blocks. English always goes last as the catch-all on `/`.

Use 302 (not 301) for the root redirect so search engines crawl all language versions.

## Step 7 — SSL via Let's Encrypt

```bash
ssh root@{IP} "certbot --nginx -d {domain} -d www.{domain} --non-interactive --agree-tos -m {userEmail}"
```

SSL is free and auto-renews every 90 days. No yearly fee.

## Step 8 — systemd services (one per language)

Write service files locally, then rsync them:

```bash
# Write locally
mkdir -p /tmp/systemd-{domain}

cat > /tmp/systemd-{domain}/website-{lang1}.service << 'EOF'
[Unit]
Description=WebsiteEmpire {lang1}
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/websiteempire/deploy/{lang1}
ExecStart=/opt/websiteempire/StaticWebsiteServe --port {port1} --lang {lang1} --images-db /opt/websiteempire/deploy/images.db
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF

# Repeat cat block for each additional language...

# Upload all service files
rsync /tmp/systemd-{domain}/ root@{IP}:/etc/systemd/system/

# Enable and start
ssh root@{IP} "systemctl daemon-reload && systemctl enable --now {all service names} && systemctl status {all service names}"
```

Verify each language responds on its port:
```bash
ssh root@{IP} "curl -s --compressed http://localhost:{port1}/{lang1}/index.html | head -3"
# repeat for each language
```

## Step 9 — Final check

```bash
# Root redirect works (should show Location: header)
ssh root@{IP} "curl -sI https://{domain}/ | grep -i location"

# Each language page serves correctly
ssh root@{IP} "curl -s --compressed https://{domain}/index.html | head -3"
# for each non-English lang:
ssh root@{IP} "curl -s --compressed https://{domain}/{lang}/index.html | head -3"
```

If all return expected HTML, deployment is complete. Tell the user the site is live at https://{domain}.

Remind the user to submit `https://{domain}/sitemap.xml` to Google Search Console.

## Redeploying after a republish

```bash
# 1. Rsync updated deploy folder
rsync -avz --progress {workingDir}/deploy/ root@{IP}:/opt/websiteempire/deploy/

# 2. Restart all language services
ssh root@{IP} "systemctl restart {all service names}"
```

Do NOT touch the nginx config during a redeploy.

## Notes
- The deploy/ folder structure is: `deploy/images.db` (shared) + `deploy/{lang}/content.db` per language
- Drogon serves gzip by default — always use `curl --compressed` when testing
- SSL auto-renews every 90 days — no action needed
- Use 302 (not 301) for root language redirect so Google crawls all language versions
