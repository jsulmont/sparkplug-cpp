# TLS/SSL Setup Guide

This guide explains how to configure TLS/SSL for secure MQTT connections with the Sparkplug C++ library.

## Overview

The library supports TLS/SSL encryption for secure communication with MQTT brokers. This includes:
- Server authentication (verify broker identity)
- Client authentication (mutual TLS)
- Encrypted data transmission

## Configuration

### Basic TLS (Server Authentication Only)

This is the most common setup - the client verifies the broker's identity:

```cpp
sparkplug::Publisher::TlsOptions tls{
    .trust_store = "/path/to/ca.crt",          // CA certificate
    .enable_server_cert_auth = true            // Verify server (default)
};

sparkplug::Publisher::Config config{
    .broker_url = "ssl://localhost:8883",      // Use ssl:// prefix
    .client_id = "my_client",
    .group_id = "MyGroup",
    .edge_node_id = "Node01",
    .tls = tls                                 // Enable TLS
};
```

### Mutual TLS (Client + Server Authentication)

For environments requiring client certificates:

```cpp
sparkplug::Publisher::TlsOptions tls{
    .trust_store = "/path/to/ca.crt",          // CA certificate
    .key_store = "/path/to/client.crt",        // Client certificate
    .private_key = "/path/to/client.key",      // Client private key
    .private_key_password = "key_password",    // If key is encrypted
    .enable_server_cert_auth = true
};
```

### Advanced Options

```cpp
sparkplug::Publisher::TlsOptions tls{
    .trust_store = "/path/to/ca.crt",
    .key_store = "/path/to/client.crt",
    .private_key = "/path/to/client.key",
    .enabled_cipher_suites = "TLS_AES_256_GCM_SHA384:TLS_CHACHA20_POLY1305_SHA256",
    .enable_server_cert_auth = true
};
```

## Mosquitto Broker Setup

### 1. Generate Certificates

```bash
# Create CA certificate
openssl req -new -x509 -days 365 -extensions v3_ca \
    -keyout ca.key -out ca.crt \
    -subj "/CN=MQTT CA"

# Create server certificate
openssl genrsa -out server.key 2048
openssl req -new -key server.key -out server.csr \
    -subj "/CN=localhost"
openssl x509 -req -in server.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out server.crt -days 365

# Create client certificate (for mutual TLS)
openssl genrsa -out client.key 2048
openssl req -new -key client.key -out client.csr \
    -subj "/CN=sparkplug_client"
openssl x509 -req -in client.csr -CA ca.crt -CAkey ca.key \
    -CAcreateserial -out client.crt -days 365
```

### 2. Configure Mosquitto

Edit `/opt/homebrew/etc/mosquitto/mosquitto.conf` (macOS) or `/etc/mosquitto/mosquitto.conf` (Linux):

```conf
# Regular MQTT (for backward compatibility)
listener 1883
protocol mqtt
allow_anonymous true

# TLS/SSL MQTT
listener 8883
protocol mqtt
cafile /path/to/ca.crt
certfile /path/to/server.crt
keyfile /path/to/server.key

# Optional: Require client certificates (mutual TLS)
# require_certificate true
# use_identity_as_username false

# Optional: TLS version
# tls_version tlsv1.2
```

### 3. Restart Mosquitto

```bash
# macOS
brew services restart mosquitto

# Linux
sudo systemctl restart mosquitto
```

### 4. Verify TLS Setup

```bash
# Test server authentication
mosquitto_pub -h localhost -p 8883 \
    --cafile /path/to/ca.crt \
    -t test -m "hello" --insecure

# Test mutual TLS
mosquitto_pub -h localhost -p 8883 \
    --cafile /path/to/ca.crt \
    --cert /path/to/client.crt \
    --key /path/to/client.key \
    -t test -m "hello"
```

## Running TLS Examples

### Build Examples

```bash
cmake --build build
```

### Update Certificate Paths

Edit the examples and replace placeholder paths:

```cpp
// In examples/publisher_tls_example.cpp and subscriber_tls_example.cpp
sparkplug::Publisher::TlsOptions tls{
    .trust_store = "/path/to/ca.crt",           // Update this
    .key_store = "/path/to/client.crt",         // Update this
    .private_key = "/path/to/client.key",       // Update this
};
```

### Run Publisher

```bash
./build/examples/publisher_tls_example
```

### Run Subscriber

```bash
./build/examples/subscriber_tls_example
```

## Troubleshooting

### Connection Refused

- Verify Mosquitto is running with TLS enabled on port 8883
- Check firewall allows port 8883
- Ensure `broker_url` uses `ssl://` prefix (not `tcp://`)

### Certificate Verification Failed

- Verify CA certificate path is correct
- Ensure server certificate is signed by the CA
- Check certificate hasn't expired: `openssl x509 -in server.crt -noout -dates`
- For self-signed certs in dev, you can disable verification (NOT for production):
  ```cpp
  .enable_server_cert_auth = false  // Development only!
  ```

### Hostname Mismatch

If connecting to an IP address or different hostname:

```bash
# Generate certificate with Subject Alternative Name (SAN)
openssl req -new -x509 -days 365 -extensions v3_ca \
    -keyout server.key -out server.crt \
    -subj "/CN=mqtt.example.com" \
    -addext "subjectAltName = DNS:localhost,IP:127.0.0.1,IP:192.168.1.100"
```

### Client Certificate Required

If the broker requires client certificates but you haven't configured them:

```bash
# Error: "peer did not return a certificate"
```

Solution: Either configure client certificates in your TlsOptions, or disable `require_certificate` in Mosquitto.

### Permission Denied

- Ensure certificate files are readable: `chmod 644 *.crt *.key`
- Verify paths are absolute, not relative

## Security Best Practices

1. **Use Strong Certificates**
   - RSA 2048-bit minimum (4096-bit recommended)
   - Modern elliptic curves (e.g., P-256)
   - SHA-256 or higher for signatures

2. **Certificate Validation**
   - Always enable server certificate verification in production
   - Keep CA certificates up to date
   - Use proper certificate chains

3. **Key Management**
   - Protect private keys with file permissions (600)
   - Use encrypted keys for sensitive deployments
   - Rotate certificates regularly

4. **Cipher Suites**
   - Prefer TLS 1.2 or higher
   - Disable weak ciphers
   - Use forward secrecy (ECDHE, DHE)

5. **Network Security**
   - Use firewall rules to restrict broker access
   - Consider VPN for additional network layer security
   - Monitor certificate expiration

## Platform Notes

### macOS (Homebrew)

Mosquitto config: `/opt/homebrew/etc/mosquitto/mosquitto.conf`

```bash
brew services start mosquitto
brew services restart mosquitto
brew services stop mosquitto
```

### Linux (systemd)

Mosquitto config: `/etc/mosquitto/mosquitto.conf`

```bash
sudo systemctl start mosquitto
sudo systemctl restart mosquitto
sudo systemctl status mosquitto
```

### Docker

```dockerfile
FROM eclipse-mosquitto:latest
COPY mosquitto.conf /mosquitto/config/mosquitto.conf
COPY ca.crt server.crt server.key /mosquitto/certs/
EXPOSE 1883 8883
```

```bash
docker run -d -p 8883:8883 \
    -v $(pwd)/certs:/mosquitto/certs \
    -v $(pwd)/mosquitto.conf:/mosquitto/config/mosquitto.conf \
    eclipse-mosquitto
```

## References

- [Eclipse Paho MQTT C SSL/TLS Documentation](https://github.com/eclipse/paho.mqtt.c)
- [Mosquitto TLS Documentation](https://mosquitto.org/man/mosquitto-tls-7.html)
- [OpenSSL Certificate Generation](https://www.openssl.org/docs/)
- [Sparkplug B Specification](https://sparkplug.eclipse.org/)
