#!/bin/bash
# Generate test certificates for Mosquitto TLS/mTLS testing
# DO NOT use these certificates in production!

set -e

CERTS_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$CERTS_DIR"

echo "Generating test certificates in: $CERTS_DIR"
echo "WARNING: These are self-signed certificates for TESTING ONLY"
echo ""

# 1. Generate CA (Certificate Authority)
echo "[1/5] Generating CA private key..."
openssl genrsa -out ca.key 2048

echo "[2/5] Generating CA certificate..."
openssl req -new -x509 -days 3650 -key ca.key -out ca.crt \
  -subj "/C=US/ST=Test/L=Test/O=SparkplugTest/CN=Test CA"

# 2. Generate Server Certificate (for Mosquitto broker)
echo "[3/5] Generating server private key..."
openssl genrsa -out server.key 2048

echo "[4/5] Generating server certificate signing request..."
openssl req -new -key server.key -out server.csr \
  -subj "/C=US/ST=Test/L=Test/O=SparkplugTest/CN=localhost"

# Create server extensions file for SAN (Subject Alternative Name)
cat > server_ext.cnf <<EOF
subjectAltName = DNS:localhost,IP:127.0.0.1
EOF

echo "[5/5] Signing server certificate with CA..."
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out server.crt -days 3650 \
  -extfile server_ext.cnf

# 3. Generate Client Certificate (for mTLS)
echo "[6/7] Generating client private key..."
openssl genrsa -out client.key 2048

echo "[7/7] Generating client certificate signing request..."
openssl req -new -key client.key -out client.csr \
  -subj "/C=US/ST=Test/L=Test/O=SparkplugTest/CN=sparkplug-client"

echo "[8/8] Signing client certificate with CA..."
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key \
  -CAcreateserial -out client.crt -days 3650

# Cleanup temporary files
rm -f server.csr client.csr server_ext.cnf ca.srl

# Set permissions
chmod 600 *.key
chmod 644 *.crt

echo ""
echo "Certificate generation complete!"
echo ""
echo "Generated files:"
echo "  ca.crt         - CA certificate (trust store)"
echo "  ca.key         - CA private key"
echo "  server.crt     - Mosquitto broker certificate"
echo "  server.key     - Mosquitto broker private key"
echo "  client.crt     - Client certificate (for mTLS)"
echo "  client.key     - Client private key (for mTLS)"
echo ""
echo "Next steps:"
echo "  1. Configure Mosquitto to use server.crt and server.key"
echo "  2. Use ca.crt in your client applications"
echo "  3. For mTLS, also use client.crt and client.key"
