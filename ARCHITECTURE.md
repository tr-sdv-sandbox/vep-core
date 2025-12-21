# Vehicle Edge Platform - Architecture

## System Overview

```
┌──────────────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                            RT / HARDWARE SIDE                                            │
├──────────────────────────────────────────────────────────────────────────────────────────────────────────┤
│                                                                                                          │
│  ┌─────────┐      ┌──────────────────┐                 ┌─────────────────┐                               │
│  │   CAN   │─────→│  vep_can_probe   │                 │  rt_dds_bridge  │                               │
│  │  vcan0  │      └────────┬─────────┘                 └────────┬────────┘                               │
│  └─────────┘               │                                    ▲                                        │
│                            │                                    │                                        │
│                            │                                    │           ┌──────────────┐             │
│                            │             ┌──────────────┐       │           │ vep_otel     │             │
│                            │             │ diagnostics  │       │           │ _probe       │             │
│                            │             │   probe      │       │           └──────┬───────┘             │
│                            │             └──────┬───────┘       │                  │                     │
│                            │                    │               │                  │                     │
├────────────────────────────┼────────────────────┼───────────────┼──────────────────┼─────────────────────┤
│                            ▼                    ▼               ▼                  ▼                     │
│  ┌──────────────────────────────────────────────────────────────────────────────────────────────────┐   │
│  │                                           DDS BUS                                                │   │
│  └──────────────────────────────────────────────────────────────────────────────────────────────────┘   │
│                  │                                                       │                              │
│                  ▼                                                       ▼                              │
│         ┌──────────────┐                                        ┌──────────────────┐                    │
│         │ vep_exporter │                                        │ kuksa_dds_bridge │                    │
│         └──────┬───────┘                                        └────────┬─────────┘                    │
│                ▼                                                         ▼                              │
│         ┌──────────────┐                                        ┌──────────────────┐                    │
│         │ Mosquitto /  │                                        │      KUKSA       │                    │
│         │     AWS      │                                        │   Databroker     │                    │
│         └──────────────┘                                        └────────┬─────────┘                    │
│                                                                          │                              │
│                                                                 ┌────────┼────────┐                     │
│                                                                 ▼        ▼        ▼                     │
│                                                              ┌─────┐ ┌─────┐ ┌─────┐                    │
│                                                              │App1 │ │App2 │ │App3 │                    │
│                                                              └─────┘ └─────┘ └─────┘                    │
│                                                                                                         │
└─────────────────────────────────────────────────────────────────────────────────────────────────────────┘
```

## Components

### Probes (Data Sources)

**vep_can_probe**
- Reads CAN frames from vcan0 (or physical CAN interface)
- Decodes using DBC files (e.g., Model3CAN.dbc)
- Maps CAN signals to VSS paths using DAG-based transformations
- Publishes to DDS topic `rt/vss/signals`

**vep_otel_probe**
- Receives OpenTelemetry metrics and logs via OTLP gRPC (port 4317)
- Extracts resource attributes (service.name, host.name) for source identification
- Source ID format: `service@host` (e.g., `vep_host_metrics@ecu-main`)
- Converts to DDS messages (gauges, counters, histograms, logs)
- Publishes to DDS topics: `rt/telemetry/gauges`, `rt/telemetry/counters`, etc.

**vep_host_metrics**
- Collects Linux system metrics (CPU, memory, disk I/O, network, filesystem)
- Follows OpenTelemetry semantic conventions for system metrics
- Exports via OTLP gRPC to vep_otel_probe
- Includes host.name resource attribute for multi-ECU identification

**diagnostics probes** 
- UDS diagnostic data
- DTC codes, freeze frames

### DDS Bus

Central message bus using CycloneDDS. All probes publish here, all consumers subscribe here.

**Topics:**
| Topic | Direction | Content |
|-------|-----------|---------|
| `rt/vss/signals` | Probes → Consumers | Sensor values |
| `rt/vss/actuators/target` | KUKSA Bridge → RT Bridge | Actuator commands |
| `rt/vss/actuators/actual` | RT Bridge → KUKSA Bridge | Actuator feedback |

### Bridges

**rt_dds_bridge**
- Bridges DDS to real-time transport layer (UDP, AVTP, SHM)
- Bidirectional: receives actuator targets, sends actuator actuals
- Loopback mode for testing (echoes targets as actuals)

**kuksa_dds_bridge**
- Bridges DDS to KUKSA databroker
- Sensors: DDS → KUKSA (vehicle apps can subscribe)
- Actuators: KUKSA set() → DDS target, DDS actual → KUKSA feedback

### Cloud Offboard

**vep_exporter**
- Subscribes to DDS topics
- Compresses and batches telemetry
- Publishes to MQTT for cloud offboarding

**Mosquitto / AWS**
- MQTT broker (Mosquitto for local testing, AWS IoT for production)
- Receives all offboarded vehicle data

### Vehicle Apps

**KUKSA Databroker**
- Central VSS signal store with gRPC API
- Apps subscribe to signals, set actuator targets
- Schema queried dynamically (no local VSS JSON needed)

**App1, App2, App3...**
- Vehicle applications using libkuksa-cpp
- Subscribe to VSS signals
- Control actuators via KUKSA

## Data Flow Examples

### Sensor Flow (e.g., Vehicle.Speed)
```
CAN → vep_can_probe → DDS → kuksa_dds_bridge → KUKSA → Apps
                       └──→ vep_exporter → MQTT → Cloud
```

### Actuator Flow (e.g., HVAC Temperature)
```
App → KUKSA set() → kuksa_dds_bridge → DDS target → rt_dds_bridge → RT
                                                           │
                    KUKSA ← kuksa_dds_bridge ← DDS actual ←┘
```

### Host Metrics Flow (OpenTelemetry)
```
vep_host_metrics → OTLP gRPC :4317 → vep_otel_probe → DDS → vep_exporter → MQTT → Cloud
                                           │
                                    source_id: "service@host"
                                    labels: {service=vep_host_metrics@ecu-main}
```

## Ports

| Service | Port | Protocol |
|---------|------|----------|
| KUKSA Databroker | 61234 | gRPC |
| Mosquitto MQTT | 1883 | MQTT |
| vep_otel_probe (OTLP) | 4317 | gRPC |
| DDS | multicast | RTPS |

## Running the Framework

```bash
# Start all components (from vehicle-edge-platform root)
./scripts/run_framework.sh

# Replay CAN data
canplayer -I config/candump.log vcan0=can0

# Monitor KUKSA signals
./scripts/run_kuksa_logger.sh
```
