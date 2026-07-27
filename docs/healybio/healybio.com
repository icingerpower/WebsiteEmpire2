# nginx site config for healybio.com — separate from biomarky.com.
# HTTP-only version (pre-certbot). After `certbot --nginx -d healybio.com -d www.healybio.com`
# certbot will inject the SSL lines and the HTTP->HTTPS redirect automatically.
#
# Canonical domain: https://healybio.com/ (non-www). www redirects to non-www.
#
# Local port map (must match the systemd units) — 8 qualifying languages:
#   en = 8090 (default / catch-all)
#   it = 8091
#   ko = 8092
#   es = 8093
#   pt = 8094
#   ja = 8095
#   de = 8096
#   fr = 8097
# Add a `location /<lang>/` block above `location /` for each new language that
# later qualifies, pointing at its assigned port.

server {
    server_name healybio.com www.healybio.com;
    listen 80;

    location = / {
        if ($http_accept_language ~* "^it") {
            return 302 /it/index.html;
        }
        if ($http_accept_language ~* "^ko") {
            return 302 /ko/index.html;
        }
        if ($http_accept_language ~* "^es") {
            return 302 /es/index.html;
        }
        if ($http_accept_language ~* "^pt") {
            return 302 /pt/index.html;
        }
        if ($http_accept_language ~* "^ja") {
            return 302 /ja/index.html;
        }
        if ($http_accept_language ~* "^de") {
            return 302 /de/index.html;
        }
        if ($http_accept_language ~* "^fr") {
            return 302 /fr/index.html;
        }
        return 302 /index.html;
    }

    location /it/ {
        proxy_pass http://127.0.0.1:8091/;
        proxy_set_header Host $host;
    }

    location /ko/ {
        proxy_pass http://127.0.0.1:8092/;
        proxy_set_header Host $host;
    }

    location /es/ {
        proxy_pass http://127.0.0.1:8093/;
        proxy_set_header Host $host;
    }

    location /pt/ {
        proxy_pass http://127.0.0.1:8094/;
        proxy_set_header Host $host;
    }

    location /ja/ {
        proxy_pass http://127.0.0.1:8095/;
        proxy_set_header Host $host;
    }

    location /de/ {
        proxy_pass http://127.0.0.1:8096/;
        proxy_set_header Host $host;
    }

    location /fr/ {
        proxy_pass http://127.0.0.1:8097/;
        proxy_set_header Host $host;
    }

    location / {
        proxy_pass http://127.0.0.1:8090;
        proxy_set_header Host $host;
    }
}
