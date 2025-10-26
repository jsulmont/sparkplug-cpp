# Sparkplug B Protocol Specification - Message Flow Summary

Based on the Eclipse Sparkplug 2.2 specification.

---

## Message Types Overview

| Message Type | Sender | QoS | Retain | Purpose |
|--------------|--------|-----|--------|---------|
| **NBIRTH** | Edge Node | 0 | false | Node birth certificate, announces Edge Node online |
| **NDEATH** | Edge Node / MQTT Server (Will) | 1 | false | Node death certificate, announces Edge Node offline |
| **DBIRTH** | Edge Node (for Device) | 0 | false | Device birth certificate, announces Device online |
| **DDEATH** | Edge Node (for Device) | 0 | false | Device death certificate, announces Device offline |
| **NDATA** | Edge Node | 0 | false | Node data updates (report by exception) |
| **DDATA** | Edge Node (for Device) | 0 | false | Device data updates (report by exception) |
| **NCMD** | Host Application | 1 | false | Commands to Edge Node |
| **DCMD** | Host Application | 1 | false | Commands to Device |
| **STATE** | Primary Host Application | varies | **true** | Host application online/offline status |

---

## Protocol Flow - Edge Node Lifecycle

### 1. Connection Phase

```text
Edge Node Actions:
1. Subscribe to: spBv1.0/group_id/NCMD/edge_node_id
2. Configure MQTT Will Message:
   - Topic: spBv1.0/group_id/NDEATH/edge_node_id
   - QoS: 1
   - Retain: false
   - Payload: Protobuf with bdSeq metric (incremented from last session)
3. Send MQTT CONNECT
4. IF Primary Host configured:
   - Subscribe to: STATE/primary_host_id
   - Wait for STATE message with "online": true
```

### 2. Birth Phase

```text
Edge Node Actions:
5. Publish NBIRTH:
   - Topic: spBv1.0/group_id/NBIRTH/edge_node_id
   - MUST include bdSeq metric (INT64, 0-255, matches Will Message)
   - MUST include seq number (starting value, typically 0)
   - MUST include ALL metrics that will EVER be published for this node
   - Metrics should include name AND alias
6. For each Device:
   - IF Device supports commands: Subscribe to spBv1.0/group_id/DCMD/edge_node_id/device_id
   - Publish DBIRTH with all device metrics
   - Group and edge_node_id MUST match NBIRTH
   - seq MUST increment from previous message
```

### 3. Operational Phase

```text
Edge Node Actions:
7. On metric value change (Report by Exception):
   - Publish NDATA (node metrics) or DDATA (device metrics)
   - Increment seq (wraps 255 → 0)
   - Use metric aliases (bandwidth optimization)
8. On receiving NCMD/DCMD:
   - Process command
   - Update metric values
   - Publish NDATA/DDATA with new values
9. On rebirth command (NCMD):
   - Increment bdSeq
   - Republish NBIRTH with all metrics
   - Republish all DBIRTHs
   - Reset seq to 0
```

### 4. Death Phase

```text
Edge Node Actions:
10. On Device disconnect:
    - Publish DDEATH for affected device
    - Timestamp marks when device went offline
11. On intentional disconnect:
    - Publish NDEATH (incremented bdSeq)
    - Send MQTT DISCONNECT
12. On unexpected disconnect:
    - MQTT Server publishes Will Message (NDEATH)
```

---

## Protocol Flow - Host Application Lifecycle

### 1. Connection Phase

```
Host Application Actions:
1. Configure MQTT Will Message:
   - Topic: STATE/host_application_id
   - Retain: true
   - Payload: JSON {"online": false, "timestamp": <UTC ms>}
2. Send MQTT CONNECT (Clean Session: true / Clean Start: true, Session Expiry: 0)
3. Subscribe to:
   - spBv1.0/group_id/# (all Sparkplug messages)
   - STATE/+  (all host application states)
```

### 2. Birth Phase

```
Host Application Actions:
4. Publish STATE birth certificate:
   - Topic: STATE/host_application_id
   - Retain: true
   - Payload: JSON {"online": true, "timestamp": <UTC ms>}
   - Timestamp MUST match Will Message timestamp
5. Ready to receive Edge Node messages
```

### 3. Operational Phase

```text
Host Application Actions:
6. On receiving NBIRTH:
   - Store bdSeq for this Edge Node
   - Mark Edge Node ONLINE
   - Mark all metrics GOOD (initial values)
   - Reset sequence validation
7. On receiving DBIRTH:
   - Mark Device ONLINE
   - Mark all device metrics GOOD
   - Validate seq incremented correctly
8. On receiving NDATA/DDATA:
   - Validate seq number (detect packet loss)
   - Update metric values
   - IF seq gap detected:
     * Start reorder timeout (typically 2-5 seconds)
     * IF timeout expires: Send NCMD rebirth request
9. On receiving NDEATH:
   - Verify bdSeq matches current session
   - Mark Edge Node OFFLINE (current UTC time)
   - Mark ALL Edge Node metrics STALE
   - Mark ALL associated Devices OFFLINE
   - Mark ALL Device metrics STALE
10. On receiving DDEATH:
    - Mark Device OFFLINE (use DDEATH timestamp)
    - Mark Device metrics STALE
```

### 4. Command Phase

```
Host Application Actions:
11. To command Edge Node:
    - Publish NCMD with metric name and new value
    - QoS: 1
12. To command Device:
    - Publish DCMD with metric name and new value
    - QoS: 1
13. To request rebirth:
    - Publish NCMD with rebirth command
    - Edge Node will republish NBIRTH/DBIRTHs
```

---

## Sequence Number Management

### bdSeq (Birth/Death Sequence)

- **Type:** INT64 metric
- **Range:** 0-255
- **Behavior:**
  - Increments on each MQTT reconnection/rebirth
  - Wraps: 255 → 0
  - MUST match between NDEATH Will Message and NBIRTH
  - Used to correlate NDEATH with specific NBIRTH session
  - Prevents duplicate NDEATH processing

### seq (Message Sequence)

- **Type:** Field in Sparkplug payload
- **Range:** 0-255
- **Behavior:**
  - Starts with value in NBIRTH/DBIRTH (typically 0)
  - Increments for EVERY message (NBIRTH, DBIRTH, NDATA, DDATA)
  - Wraps: 255 → 0
  - Used to detect packet loss
  - Host validates ordering with configurable reorder timeout

---

## Required Metrics

### NBIRTH Must Include:

1. **bdSeq** - INT64, 0-255, matches Will Message
2. **seq** - Starting sequence number
3. **ALL metrics** that will ever be published for this Edge Node
4. Each metric should have:
   - **name** - String identifier
   - **alias** - Numeric identifier (for bandwidth optimization)
   - **datatype** - Sparkplug DataType enum
   - **timestamp** - UTC milliseconds since epoch
   - **value** - Current value

### NDATA Can Use:

- Metric **aliases only** (no names required)
- Report by Exception (only changed metrics)
- Inherits metric definitions from NBIRTH

---

## Primary Host Application in Multi-Server Topology

```
Scenario: Multiple MQTT Servers, Primary Host for coordination

Primary Host Behavior:
1. Connect to each MQTT Server
2. Publish STATE birth on EACH server (Retain: true)
3. Coordinate Edge Node command authority

Edge Node Behavior:
1. Configure with Primary Host ID
2. Subscribe to STATE/<primary_host_id> on current server
3. Wait for STATE message with "online": true before publishing NBIRTH
4. If receives STATE with "online": false AND timestamp >= previous "online": true:
   - Disconnect from current server
   - Connect to next server in list
   - Repeat STATE check
5. If timestamp < previous: Ignore (stale death message)
```

---

## Timing Requirements

1. **All timestamps:** UTC time in milliseconds since Unix Epoch
2. **NTP synchronization:** Required for all Sparkplug participants
3. **Reorder Timeout:** 2-5 seconds typical (configurable)
4. **STATE timestamp:** Must match between birth and will messages

---

## Key Conformance Rules (MUST/SHOULD)

### Edge Nodes MUST:

- Subscribe to NCMD before publishing NBIRTH
- Include bdSeq in both NDEATH Will and NBIRTH
- Include ALL metrics in NBIRTH that will ever be published
- Increment seq for every message
- Set NDEATH as MQTT Will Message before connecting

### Host Applications MUST:

- Mark all metrics STALE upon receiving NDEATH
- Validate sequence numbers
- Use Clean Session/Clean Start with Session Expiry = 0
- Publish STATE with Retain: true

### All Participants MUST:

- Use UTC timestamps
- Follow case-sensitive topic naming
- Respect QoS settings per message type

### All Participants SHOULD NOT:

- Create IDs differing only in case
- Create metric names differing only in case

---

## Implementation Notes

This specification is based on the Eclipse Sparkplug 2.2 standard. The implementation in this codebase (`sparkplug-cpp`) follows these requirements in the `Publisher` and `Subscriber` classes.

### Key Implementation Details:

- **Publisher** handles NBIRTH/NDEATH lifecycle and automatic sequence management
- **Subscriber** validates sequence numbers and tracks node state
- **PayloadBuilder** provides type-safe metric construction
- **Topic** parses and validates Sparkplug topic namespace
- Thread-safe operations allow concurrent method calls from multiple threads
