# Sparkplug B Protocol Specification - Message Flow Summary

Based on the Eclipse Sparkplug 2.2 specification.

---

## Message Types Overview

## Protocol Participants

### MQTT Server (Broker)
**Role:** Transport backbone for all Sparkplug traffic. Maintains session state, delivers retained Will (Death) messages, and enforces ACLs.  
**Responsibilities:**
- Maintain client sessions and deliver **Will** messages on ungraceful disconnects.
- Retain Host `STATE/host_id` messages per QoS/retain rules.
- Optionally enforce ACLs to constrain who may publish/subscribe to `NCMD`/`DCMD`/`STATE`.

**Key Interactions:**
- Delivers **NDEATH** on behalf of Edge Nodes via MQTT Will.
- Retains latest **STATE** per Host for Primary Host discovery in multi-broker topologies.

---

### Sparkplug Host Application
**Role:** Primary consumer/command source for Edge/Device metrics; authoritative view of topology and state.  
**Responsibilities:**
- Establish a **clean** MQTT session (`Clean Session=true` for 3.1.1; `Clean Start=true`, `Session Expiry=0` for 5.0).
- Register **Will** on topic `STATE/<host_id>` with payload `{"online": false, "timestamp": <ts>}`.
- **Subscribe** to Sparkplug namespace and `STATE/<host_id>` **before** publishing own `STATE` online.
- Publish **STATE** (JSON UTF‑8) with `{"online": true, "timestamp": <same_ts_as_Will>}`.
- On Edge **NDEATH**, immediately mark: Edge **offline**, Node/Device metrics **STALE** (Host **UTC** time).

**Publishes:**
- `STATE/<host_id>` (online/offline).  
- `NCMD` and `DCMD` commands (when authorized).

**Subscribes:**
- `spBv1.0/#`, `STATE/<host_id>` (and optionally other Hosts’ STATE for HA awareness).

**QoS/Retain:**
- `STATE` typically **QoS 1**, **Retain=true**.

---

### Primary Host Application (Concept)
**Role:** The designated Host allowed to command nodes/devices; used by Edges for broker selection in multi-server topologies.  
**Responsibilities:**
- Same as Host, plus: remain **discoverable** via retained `STATE` across all active brokers.
- Coordinate **message ordering** so Edges can verify Host liveness and migrate brokers if needed.

**Edge Behavior w.r.t. Primary Host:**
- If retained `STATE/<primary_host_id>` shows `online=false` (with >= timestamp), Edge **MUST** publish **NDEATH** and reconnect/select another broker.

---

### Sparkplug Edge Node
**Role:** Gateway or MQTT-native publisher that hosts one or more Devices and their metrics.  
**Responsibilities:**
- Establish a **clean** session and register **Will** as **NDEATH** on `spBv1.0/<group_id>/NDEATH/<edge_node_id>` with **Protobuf** payload containing **`bdSeq`**.
- Before **NBIRTH**, **subscribe** `spBv1.0/<group_id>/NCMD/<edge_node_id>` (**QoS 1**) and, if applicable, `STATE/<primary_host_id>` and `spBv1.0/<group_id>/DCMD/#`.
- Publish **NBIRTH** first after connect: **QoS 0**, **Retain=false**, payload includes **`seq ∈ [0..255]`**.
- Publish **NDATA** on **RBE** (not periodic) with incrementing `seq` (wrap at 255).

**Publishes:** `NBIRTH`, `NDATA`, `NDEATH`.  
**Subscribes:** `NCMD` (mandatory), `DCMD` (optional, if proxying commands to devices), `STATE/<primary_host_id>`.  
**Disconnects:** On intentional disconnect, **publish NDEATH first**; optional DISCONNECT to suppress Will. Duplicate NDEATH (same `bdSeq`) **must be ignored** by receivers.

---

### Sparkplug Device (MQTT-Enabled Device)
**Role:** Optional leaf that publishes its own metrics via the Edge’s namespace.  
**Responsibilities:**
- If outputs are writable, **subscribe** `spBv1.0/<group_id>/DCMD/<edge_node_id>/<device_id>` (**QoS 1**).
- Publish **DBIRTH** only **after** the parent **NBIRTH** (same session) and with matching `group_id`/`edge_node_id`.
- **DBIRTH** payload Protobuf with **`seq ∈ [0..255]`**, **QoS 0**, **Retain=false**; publish **DDATA** on RBE; **DDEATH** on offline.

**Publishes:** `DBIRTH`, `DDATA`, `DDEATH`.  
**Subscribes:** `DCMD` (**QoS 1** when applicable).

---



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

---
# Developer Addendum: Payloads and Messaging Rules

# Sparkplug Payloads and Messaging Rules (Developer Addendum)

This document consolidates the **normative payload and messaging rules** relevant to Sparkplug B
Host Applications and Edge Nodes. It omits DataSet and Template definitions and represents payloads in **JSON**.

---

## 1. Payload Definition

### 1.1 Payload Object

A Sparkplug payload contains:

```json
{
  "timestamp": 1668612345678,
  "seq": 42,
  "metrics": [ ... ]
}
```

- `timestamp` — UTC milliseconds since epoch.  
  - **MUST** be present in NBIRTH, DBIRTH, NDATA, and DDATA.  
  - **MAY** be included in NCMD and DCMD.  
- `seq` — unsigned 8-bit integer (0–255). Used for message sequencing.  
- `metrics[]` — array of Metric objects (see below).

---

## 2. Metric Definition

A **Metric** object conveys a single point of data or metadata.

```json
{
  "name": "Temperature",
  "alias": 1,
  "datatype": "Float",
  "value": 23.4,
  "timestamp": 1668612345678,
  "metadata": {
    "units": "°C"
  },
  "properties": { ... }
}
```

### Normative rules

- Each metric **MUST** have a `name` and `datatype` in NBIRTH/DBIRTH.  
- Each metric **MUST** have a `value` in NBIRTH/DBIRTH messages.  
- A metric `timestamp`, if present, **MUST** be UTC.  
- Subsequent NDATA/DDATA updates **SHOULD NOT** repeat the `datatype` unless it changes.  
- Hierarchical naming using `/` separators **MAY** be used.  
- Boolean, numeric, string, and bytes datatypes are supported.  
- Arrays **MAY** be used if both producer and consumer agree on type.  

---

## 3. PropertySet and PropertyValue

Properties provide metadata extensibility for metrics or components.

### PropertyValue

```json
{
  "key": "scaleFactor",
  "value": 0.1,
  "type": "Double"
}
```

Rules:

- `key` (string) — **MUST** be unique within the property set.  
- `value` — **MUST** match the declared `type`.  
- `type` — **MUST** be one of the standard Sparkplug scalar datatypes.  

### PropertySet

```json
{
  "keys": ["scaleFactor", "displayPrecision"],
  "values": [
    { "key": "scaleFactor", "value": 0.1, "type": "Double" },
    { "key": "displayPrecision", "value": 2, "type": "Int32" }
  ]
}
```

Rules:

- A PropertySet **MAY** appear in a Metric or in MetaData.  
- A PropertySet **MUST NOT** contain duplicate keys.  
- PropertySets **MAY** be nested if referenced by name.  

---

## 4. MetaData

Optional context for metrics.

```json
{
  "units": "°C",
  "displayFormat": "%.1f",
  "description": "Ambient temperature",
  "enableHistorical": true
}
```

Rules:

- All MetaData fields are optional.  
- `units` and `displayFormat` **SHOULD** be consistent with datatype.  
- Host Applications **MUST** preserve unknown metadata fields.  

---

## 5. Normative Messaging Rules

### 5.1 QoS and Retain

| Message Type | QoS | Retain | Notes |
|---------------|-----|--------|-------|
| NBIRTH / DBIRTH | 1 | true | Defines entity and metrics. |
| NDATA / DDATA | 0 or 1 | false | Report-by-exception updates. |
| NCMD / DCMD | 0 or 1 | false | Commands to edge node or device. |
| NDEATH / DDEATH | 1 | false | Sent on controlled shutdown. |
| STATE | 1 | true | Host STATE indication (JSON payload). |

Rules:

- All BIRTH and DEATH messages **MUST** use QoS 1.  
- All BIRTH messages **MUST** be published with `retain=true`.  
- All DEATH messages **MUST** be published with `retain=false`.  
- NDATA/DDATA **MAY** use QoS 0 for performance but SHOULD use QoS 1 when reliability is required.  
- Host STATE topics **MUST** be retained to indicate last-known host status.  

### 5.2 Sequence Numbers

- Each Edge Node **MUST** maintain an 8‑bit `seq` counter incremented for every NDATA, NCMD, or DDATA message.  
- Counter wraps from 255 → 0.  
- Host Applications **MUST** detect gaps to identify missing messages.  
- The first NBIRTH after connection **MUST** start at `seq=0`.  

### 5.3 Timestamp Semantics

- All timestamps are milliseconds since epoch (UTC).  
- Edge Nodes **MUST** timestamp metrics as close to acquisition as possible.  
- Host Applications **MUST NOT** rewrite metric timestamps unless acting as data originators.  

---

## 6. Example

### NBIRTH Example

```json
{
  "timestamp": 1668612000000,
  "seq": 0,
  "metrics": [
    {
      "name": "Temperature",
      "datatype": "Float",
      "value": 23.4,
      "metadata": { "units": "°C" },
      "properties": {
        "scaleFactor": { "value": 0.1, "type": "Double" }
      }
    },
    {
      "name": "Humidity",
      "datatype": "Float",
      "value": 41.2,
      "metadata": { "units": "%" }
    }
  ]
}
```

---

### NDATA Example

```json
{
  "timestamp": 1668612010000,
  "seq": 1,
  "metrics": [
    { "name": "Temperature", "value": 23.6 },
    { "name": "Humidity", "value": 41.5 }
  ]
}
```

---

### STATE Example

```json
{
  "online": true,
  "timestamp": 1668612050000,
  "bdSeq": 42
}
```

Rules:

- Published by Host Application to `spBv1.0/STATE/<GroupId>` with QoS 1 and `retain=true`.  
- `online` **MUST** be `true` on startup and `false` on orderly shutdown.  
- `bdSeq` identifies the current BIRTH/DEATH sequence counter for the node.  

---

**End of Developer Addendum**
