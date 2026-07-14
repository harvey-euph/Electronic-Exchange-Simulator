#!/usr/bin/env bash
set -e

DOMAIN=${1:-harvey-exchange.duckdns.org}
EMAIL=${2:-admin@$DOMAIN}

echo "============================================================"
echo " Setting up Let's Encrypt SSL Certificate for $DOMAIN"
echo "============================================================"

# Ensure Certbot is installed
if ! command -v certbot &> /dev/null; then
    echo "Installing certbot..."
    sudo apt-get update
    sudo apt-get install -y certbot
fi

# Create the webroot directory for ACME challenge
echo "Preparing webroot directory..."
sudo mkdir -p /opt/certbot/www
sudo chown -R $USER:$USER /opt/certbot/www

echo "Requesting SSL certificate..."
# Request the certificate using webroot (Nginx inside container serves the files)
sudo certbot certonly --webroot -w /opt/certbot/www -d "$DOMAIN" --email "$EMAIL" --agree-tos --no-eff-email

echo "Certificate obtained! Restarting exchange container to apply HTTPS config..."
# We restart the container so docker-entrypoint.sh detects the new certs and swaps the Nginx config
sudo docker compose restart exchange

echo "============================================================"
echo " SSL Setup Complete! "
echo " Your exchange is now securely available at https://$DOMAIN"
echo " (Certificates will auto-renew. You can test renewal with 'sudo certbot renew --dry-run')"
echo "============================================================"
