#!/bin/bash
set -e

echo "=== Exchange Container Starting ==="

# Apply system tuning if running as root (privileged container)
if [ "$EUID" -eq 0 ] && [ "${SKIP_TUNING:-0}" != "1" ]; then
    source /opt/exchange/scripts/system-tuning.sh
    apply_system_tuning 2>/dev/null || echo "Warning: Some system tuning could not be applied (expected in containers)"
fi

# Configure Nginx dynamically based on SSL cert presence
DOMAIN=${DOMAIN_NAME:-harvey-exchange.duckdns.org}
CERT_PATH="/etc/letsencrypt/live/$DOMAIN/fullchain.pem"

rm -f /etc/nginx/sites-enabled/default

if [ -f "$CERT_PATH" ]; then
    echo "=== SSL Certificates found for $DOMAIN. Enabling HTTPS. ==="
    sed -e "s|__FRONTEND_DIST_PATH__|/opt/exchange/web/dist|g" \
        -e "s|__DOMAIN_NAME__|$DOMAIN|g" \
        /opt/exchange/nginx/exchange-https.conf > /etc/nginx/sites-available/exchange
else
    echo "=== No SSL Certificates found for $DOMAIN. Falling back to HTTP. ==="
    echo "    (Run scripts/setup-ssl.sh on the host to enable HTTPS)"
    sed -e "s|__FRONTEND_DIST_PATH__|/opt/exchange/web/dist|g" \
        /opt/exchange/nginx/exchange-http.conf > /etc/nginx/sites-available/exchange
fi

ln -sf /etc/nginx/sites-available/exchange /etc/nginx/sites-enabled/exchange

# Start nginx in the background
nginx &

# Start exchange services (launches daemons in background, then exits)
/opt/exchange/run-services

echo "=== All services started. Container staying alive. ==="

# Keep the container alive. The main process must not exit or Docker
# will think the container has stopped.
# Use 'tail -f /dev/null' — it blocks forever and is signal-friendly.
exec tail -f /dev/null
