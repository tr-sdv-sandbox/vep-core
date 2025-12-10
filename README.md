# COVESA IFEX VDR Integration

Integration example demonstrating a complete vehicle-to-cloud telemetry pipeline using:

- **libvssdag** - CAN-to-VSS signal transformation with DBC files
- **vdr-light** - DDS-based vehicle data readout framework
- **Protobuf + Zstd** - Bandwidth-efficient transfer encoding
- **MQTT** - Vehicle-to-cloud message transport

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              Vehicle (Onboard)                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────────┐     ┌─────────────────────────────────────────┐   │
│  │   vdr_can_simulator │     │              DDS Domain                 │   │
│  │                     │     │                                         │   │
│  │  CAN Data Generator │──▶  │  rt/vss/signals                        │   │
│  │         +           │     │  rt/events/vehicle                     │   │
│  │  libvssdag (DBC→VSS)│     │  rt/telemetry/{gauges,counters,...}    │   │
│  └─────────────────────┘     └───────────────────┬─────────────────────┘   │
│                                                  │                         │
│                                                  ▼                         │
│                              ┌─────────────────────────────────────────┐   │
│                              │            vep_exporter                 │   │
│                              │                                         │   │
│                              │  SubscriptionManager (from vdr-light)   │   │
│                              │              +                          │   │
│                              │  CompressedMqttSink (protobuf + zstd)   │   │
│                              └───────────────────┬─────────────────────┘   │
│                                                  │                         │
└──────────────────────────────────────────────────┼─────────────────────────┘
                                                   │ MQTT (compressed)
                                                   ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Cloud / Backend                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────┐                                                    │
│  │   Mosquitto Broker  │◀────────────────────────────────────────────────── │
│  └──────────┬──────────┘                                                    │
│             │                                                               │
│             ▼                                                               │
│  ┌─────────────────────┐     Decompress → Decode Protobuf → JSON/Storage   │
│  │ vep_mqtt_receiver   │                                                    │
│  └─────────────────────┘                                                    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Components

### Probes (Data Producers)

**vep_can_probe** - CAN-to-VSS probe:
- Reads CAN frames from SocketCAN or IEEE 1722 AVTP interfaces
- Transforms CAN signals to VSS using libvssdag
- Publishes to DDS topic `rt/vss/signals`

```bash
# SocketCAN (default)
./vep_can_probe --config mappings.yaml --interface vcan0 --dbc model3.dbc

# IEEE 1722 AVTP over Ethernet
./vep_can_probe --config mappings.yaml --interface eth0 --dbc model3.dbc --transport avtp
```

**vdr_can_simulator** - Simulates CAN bus data from a vehicle:
- Generates realistic vehicle signals (speed, SOC, motor temps, doors, etc.)
- Uses Tesla Model 3 DBC file for CAN encoding
- Transforms CAN signals to VSS using libvssdag
- Publishes to DDS topic `rt/vss/signals`

**vep_otel_probe** - OpenTelemetry bridge:
- Receives metrics and logs via OTLP gRPC (port 4317)
- Extracts resource attributes (service.name, host.name) for source identification
- Converts to DDS messages (gauges, counters, histograms, logs)
- Source ID format: `service@host` for multi-ECU environments

### Applications

**vep_exporter** - Exports telemetry to cloud:
- Subscribes to DDS topics using vdr-light's SubscriptionManager
- Batches signals for efficiency
- Encodes to lean protobuf format
- Compresses with zstd
- Publishes to MQTT broker

### Tools

**vep_mqtt_receiver** - MQTT receiver for testing:
- Subscribes to MQTT topics
- Decompresses with zstd
- Decodes protobuf (signals, events, metrics, logs)
- Displays in human-readable or JSON format
- Shows service labels for multi-source identification

**vep_host_metrics** - Linux host metrics collector:
- Collects CPU, memory, disk I/O, network, filesystem metrics
- Follows OpenTelemetry semantic conventions
- Exports via OTLP gRPC to `vep_otel_probe`
- Includes service and host identification for multi-ECU environments

## Prerequisites

Install dependencies:

```bash
sudo ./install_deps.sh
```

Ensure sibling libraries are available:

```
~/BALI/
├── vdr-light/           # Required
├── libvssdag/           # Required
├── libvss-types/        # Required (may be pulled by libvssdag)
└── covesa-ifex-vdr-integration/  # This repo
```

## Building

```bash
cmake -B build
cmake --build build -j$(nproc)
```

## Running

### 1. Start MQTT Broker

```bash
docker-compose -f docker/docker-compose.yml up -d
```

### 2. Start MQTT Receiver

```bash
./build/vep_mqtt_receiver --verbose
```

### 3. Start VDR Exporter

```bash
./build/vep_exporter
```

### 4. Start CAN Simulator

```bash
./build/vdr_can_simulator --config config/model3_mappings_dag.yaml
```

You should see signals flowing:
1. CAN simulator generates vehicle data
2. libvssdag transforms CAN→VSS
3. DDS publishes to domain
4. vep_exporter batches, compresses, sends to MQTT
5. vep_mqtt_receiver receives, decompresses, displays

## Configuration

### vep_exporter

```yaml
# config/exporter.yaml
mqtt:
  broker: localhost
  port: 1883
  client_id: vep_exporter
  topic_prefix: v1/telemetry

batching:
  max_signals: 100
  timeout_ms: 100

compression:
  level: 3

subscriptions:
  vss_signals: true
  events: true
  gauges: true
  counters: true
  histograms: true
  logs: false
```

### vdr_can_simulator

Uses libvssdag YAML configuration. See `config/model3_mappings_dag.yaml`.

## Transfer Protocol

The transfer protocol (`proto/transfer.proto`) is optimized for bandwidth:

- **Delta timestamps**: Base timestamp + per-signal deltas
- **Path interning**: Optional path ID instead of full string
- **Batching**: Multiple signals per message
- **Compact values**: Uses protobuf's efficient wire format
- **Zstd compression**: Typically 60-80% size reduction

Example compressed batch:
```
Original: 2048 bytes (100 signals, full paths)
Compressed: 412 bytes (80% reduction)
```

## Permissions

### AVTP Transport (IEEE 1722)

AVTP uses raw Ethernet sockets which require elevated privileges:

**Standalone:**
```bash
# Option 1: Run as root
sudo ./vep_can_probe --transport avtp --interface eth0 ...

# Option 2: Grant CAP_NET_RAW capability (preferred)
sudo setcap cap_net_raw+ep ./vep_can_probe
./vep_can_probe --transport avtp --interface eth0 ...
```

**Container:**
```bash
# Docker/Podman - add NET_RAW capability and host network
docker run --cap-add NET_RAW --network host myimage vep_can_probe --transport avtp ...

# Or use privileged mode (includes all capabilities)
docker run --privileged --network host myimage vep_can_probe --transport avtp ...
```

### Virtual CAN (SocketCAN)

Virtual CAN requires kernel module setup on the host:
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

For containers, use `--network host` to access the host's vcan interfaces.

## License

Apache-2.0
