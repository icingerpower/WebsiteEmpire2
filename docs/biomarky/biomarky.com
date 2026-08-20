# Redirect www → non-www (301)
server {
    server_name www.biomarky.com;
    listen 443 ssl;
    ssl_certificate /etc/letsencrypt/live/biomarky.com/fullchain.pem; # managed by Certbot
    ssl_certificate_key /etc/letsencrypt/live/biomarky.com/privkey.pem; # managed by Certbot
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;
    return 301 https://biomarky.com$request_uri;

}

# Main server (non-www only)
server {
    server_name biomarky.com;

    location = / {
        if ($http_accept_language ~* "^ja") {
            return 302 /ja/index.html;
        }
        if ($http_accept_language ~* "^fr") {
            return 302 /fr/index.html;
        }
        if ($http_accept_language ~* "^de") {
            return 302 /de/index.html;
        }
        if ($http_accept_language ~* "^es") {
            return 302 /es/index.html;
        }
        if ($http_accept_language ~* "^pt") {
            return 302 /pt/index.html;
        }
        if ($http_accept_language ~* "^it") {
            return 302 /it/index.html;
        }
        return 302 /index.html;
    }

    location /es/ {
        proxy_pass http://127.0.0.1:8084/;
        proxy_set_header Host $host;
    }

    location /pt/ {
        proxy_pass http://127.0.0.1:8085/;
        proxy_set_header Host $host;
    }

    location /ja/ {
        proxy_pass http://127.0.0.1:8083/;
        proxy_set_header Host $host;
    }

    location /fr/ {
        proxy_pass http://127.0.0.1:8081/;
        proxy_set_header Host $host;
    }

    location /de/ {
        proxy_pass http://127.0.0.1:8082/;
        proxy_set_header Host $host;
    }

    location /it/ {
        proxy_pass http://127.0.0.1:8086/;
        proxy_set_header Host $host;
    }

    location / {
        proxy_pass http://127.0.0.1:8080;
        proxy_set_header Host $host;
    }

    listen 443 ssl;
    ssl_certificate /etc/letsencrypt/live/biomarky.com/fullchain.pem; # managed by Certbot
    ssl_certificate_key /etc/letsencrypt/live/biomarky.com/privkey.pem; # managed by Certbot
    include /etc/letsencrypt/options-ssl-nginx.conf;
    ssl_dhparam /etc/letsencrypt/ssl-dhparams.pem;

}

# HTTP → HTTPS + www → non-www (all cases → canonical)
server {
    if ($host = www.biomarky.com) {
        return 301 https://$host$request_uri;
    } # managed by Certbot


    if ($host = biomarky.com) {
        return 301 https://$host$request_uri;
    } # managed by Certbot


    server_name biomarky.com www.biomarky.com;
    listen 80;
    return 301 https://biomarky.com$request_uri;




}
