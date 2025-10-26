#!/bin/bash
# Start Mosquitto with TLS/mTLS test configuration

CERTS_DIR="$(cd "$(dirname "$0")" && pwd)"
MOSQUITTO_CONF="$CERTS_DIR/mosquitto_test.conf"

echo "Starting Mosquitto with TLS test configuration..."
echo "Config: $MOSQUITTO_CONF"
echo ""
echo "Listeners:"
echo "  Port 1883: Plain MQTT (no encryption)"
echo "  Port 8883: TLS/SSL (server authentication)"
echo ""
echo "To enable mTLS (mutual authentication):"
echo "  Edit mosquitto_test.conf and set require_certificate to true"
echo ""
echo "Press Ctrl+C to stop"
echo ""

# Stop any existing Mosquitto service
echo "Stopping Homebrew Mosquitto service (if running)..."
brew services stop mosquitto 2>/dev/null || true

# Start Mosquitto with test config
/opt/homebrew/opt/mosquitto/sbin/mosquitto -c "$MOSQUITTO_CONF" -v
