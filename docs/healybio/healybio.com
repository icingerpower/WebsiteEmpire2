# nginx site config for healybio.com — separate from biomarky.com.
#
# Canonical domain: https://healybio.com/ (non-www). www redirects to non-www.
#
# Local port map (must match the systemd units) — 12 qualifying languages:
#   en = 8090 (default / catch-all)
#   it = 8091
#   ko = 8092
#   es = 8093
#   pt = 8094
#   ja = 8095
#   de = 8096
#   fr = 8097
#   hi = 8098
#   pl = 8099
#   tr = 8100
#   ar = 8101
# Add a `location /<lang>/` block above `location /` for each new language that
# later qualifies, pointing at its assigned port.

server {
    server_name healybio.com www.healybio.com;

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
        if ($http_accept_language ~* "^hi") {
            return 302 /hi/index.html;
        }
        if ($http_accept_language ~* "^pl") {
            return 302 /pl/index.html;
        }
        if ($http_accept_language ~* "^tr") {
            return 302 /tr/index.html;
        }
        if ($http_accept_language ~* "^ar") {
            return 302 /ar/index.html;
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

    location /hi/ {
        proxy_pass http://127.0.0.1:8098/;
        proxy_set_header Host $host;
    }

    location /pl/ {
        proxy_pass http://127.0.0.1:8099/;
        proxy_set_header Host $host;
    }

    location /tr/ {
        proxy_pass http://127.0.0.1:8100/;
        proxy_set_header Host $host;
    }

    location /ar/ {
        proxy_pass http://127.0.0.1:8101/;
        proxy_set_header Host $host;
    }

    location / {
        proxy_pass http://127.0.0.1:8090;
        proxy_set_header Host $host;
    }

    listen 443 ssl; # managed by Certbot
    ssl_certificate /etc/letsencrypt/live/healybio.com/fullchain.pem; # managed by Certbot
    ssl_certificate_key /etc/letsencrypt/live/healybio.com/privkey.pem; # managed by Certbot
    include /etc/letsencrypt/options-ssl-nginx.conf; # managed by Certbot
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem; # managed by Certbot


}


server {
    if ($host = www.healybio.com) {
        return 301 https://$host$request_uri;
    } # managed by Certbot


    if ($host = healybio.com) {
        return 301 https://$host$request_uri;
    } # managed by Certbot


    server_name healybio.com www.healybio.com;
    listen 80;
    return 404; # managed by Certbot




}
