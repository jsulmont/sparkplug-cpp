# TLS/mTLS Test Certificates

This directory contains test certificates for Mosquitto TLS/mTLS testing.

**WARNING:** These are self-signed certificates for TESTING ONLY. Do NOT use in production!

## Quick Start

### 1. Generate Certificates (if not already done)

```bash
./generate_certs.sh
```

This creates:

- `ca.crt` / `ca.key` - Certificate Authority (trust store)
- `server.crt` / `server.key` - Mosquitto broker certificates
- `client.crt` / `client.key` - Client certificates for mTLS

### 2. Start Mosquitto with TLS Support

```bash
./start_mosquitto_test.sh
```

This starts Mosquitto with:

- **Port 1883**: Plain MQTT (no encryption)
- **Port 8883**: TLS/SSL (server authentication only by default)

### 3. Build and Run Examples

```bash
# Build the project
cd ..
cmake --build build

# Terminal 1: Run TLS subscriber
./build/examples/subscriber_tls_example

# Terminal 2: Run TLS publisher
./build/examples/publisher_tls_example
```

## Testing Scenarios

### TLS Only (Server Authentication)

Default configuration. Server proves its identity to clients.

The broker is configured with `require_certificate false` by default, so clients don't need certificates.

To test TLS-only mode without client certificates, edit the examples to comment out:

```cpp
.key_store = "../certs/client.crt",
.private_key = "../certs/client.key",
```

### mTLS (Mutual Authentication)

Both server and client prove their identities.

1. Edit `mosquitto_test.conf`:

   ```text
   require_certificate true
   ```

2. Restart Mosquitto:

   ```bash
   ./start_mosquitto_test.sh
   ```

3. Run examples (they already have client certificates configured)

### Plain MQTT (No TLS)

Use the standard examples without TLS:

```bash
./build/examples/publisher_example  # Connects to port 1883
```

## Files

| File | Description |
|------|-------------|
| `ca.crt` | CA certificate (trust store for clients) |
| `ca.key` | CA private key |
| `server.crt` | Mosquitto broker certificate |
| `server.key` | Mosquitto broker private key |
| `client.crt` | Client certificate (for mTLS) |
| `client.key` | Client private key (for mTLS) |
| `mosquitto_test.conf` | Mosquitto configuration file |
| `generate_certs.sh` | Script to regenerate certificates |
| `start_mosquitto_test.sh` | Script to start test broker |

## Mosquitto Configuration

The `mosquitto_test.conf` file is configured to:

- Allow both plain (1883) and TLS (8883) connections
- Use the generated server certificates
- Allow anonymous connections (for testing)
- Support both TLS and mTLS modes

## Testing with mosquitto_pub/sub

You can also test with command-line tools:

```bash
# TLS subscriber (verify server only)
mosquitto_sub -h localhost -p 8883 \
  --cafile certs/ca.crt \
  -t 'spBv1.0/#' -v

# mTLS subscriber (mutual authentication)
mosquitto_sub -h localhost -p 8883 \
  --cafile certs/ca.crt \
  --cert certs/client.crt \
  --key certs/client.key \
  -t 'spBv1.0/#' -v

# Plain MQTT subscriber (no TLS)
mosquitto_sub -h localhost -p 1883 \
  -t 'spBv1.0/#' -v
```

## Troubleshooting

### "Failed to connect" errors

1. Check Mosquitto is running:

   ```bash
   ps aux | grep mosquitto
   ```

2. Check Mosquitto logs (it runs in foreground with verbose output)

3. Verify certificate paths are correct

### Certificate verification errors

- Ensure you're using `ca.crt` as the trust store
- Check server certificate is valid for `localhost`
- Verify certificate files have correct permissions (600 for .key files)

### Port already in use

Stop any existing Mosquitto service:

```bash
brew services stop mosquitto
# Or kill the process
killall mosquitto
```

## Regenerating Certificates

If you need fresh certificates:

```bash
rm *.crt *.key
./generate_certs.sh
```

Then restart Mosquitto:

```bash
./start_mosquitto_test.sh
```
