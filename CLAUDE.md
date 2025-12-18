# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build from vehicle-edge-platform top-level (required - depends on sibling components)
cd vehicle-edge-platform
./build-all.sh

# Run tests
cd build && ctest --output-on-failure

# Run vep-core specific tests
cd build/vep-core && ctest --output-on-failure
```

vep-core cannot be built standalone - it depends on vep_idl, vep_dds_common, vssdag, and kuksa targets from sibling components.

## Component Structure

```
vep-core/
├── probes/                    # Data source probes
│   ├── vep_can_probe/         # CAN → VSS → DDS (SocketCAN or AVTP)
│   ├── vep_otel_probe/        # OTLP gRPC :4317 → DDS
│   └── vep_avtp_probe/        # IEEE 1722 AVTP → DDS (raw frames)
├── bridges/                   # DDS bridges to external systems
│   ├── common/                # Shared code (rt_transport)
│   ├── exporter_common/       # Reusable exporter libraries
│   │   ├── include/           # TransportSink, Compressor, BatchBuilder, WireEncoder
│   │   └── src/
│   ├── kuksa_dds_bridge/      # KUKSA ↔ DDS bidirectional
│   ├── rt_dds_bridge/         # DDS ↔ RT transport
│   └── vep_exporter/          # DDS → compressed MQTT (one-way)
├── tools/                     # Test/debug utilities
│   ├── vep_mqtt_receiver/     # MQTT → decompress → display
│   └── vep_host_metrics/      # Linux metrics → OTLP gRPC
├── proto/
│   └── transfer.proto         # Wire format for MQTT transfer
├── tests/
│   └── integration/           # Integration tests (some require Docker)
└── config/                    # Test configuration files
```

## Key Binaries

| Binary | Location | Purpose |
|--------|----------|---------|
| `vep_can_probe` | `probes/vep_can_probe/` | CAN frames → VSS signals → DDS |
| `vep_otel_probe` | `probes/vep_otel_probe/` | OTLP metrics/logs → DDS |
| `vep_avtp_probe` | `probes/vep_avtp_probe/` | IEEE 1722 AVTP → DDS |
| `vep_exporter` | `bridges/vep_exporter/` | DDS → batched/compressed MQTT |
| `kuksa_dds_bridge` | `bridges/kuksa_dds_bridge/` | KUKSA databroker ↔ DDS |
| `rt_dds_bridge` | `bridges/rt_dds_bridge/` | DDS ↔ RT transport (loopback for testing) |
| `vep_mqtt_receiver` | `tools/vep_mqtt_receiver/` | MQTT receiver/decoder |
| `vep_host_metrics` | `tools/vep_host_metrics/` | Linux metrics collector |

## Libraries

| Library | Purpose |
|---------|---------|
| `vep_exporter_common` | Wire encoding (DDS→Protobuf), batching, compression |
| `vep_mqtt_sink` | MQTT transport sink (reusable for other exporters) |
| `vep_exporter_lib` | Legacy integrated MQTT exporter |
| `kuksa_dds_bridge_lib` | KUKSA ↔ DDS bridging logic |
| `rt_dds_bridge_lib` | DDS ↔ RT transport bridging |
| `transfer_proto` | Compiled transfer.proto for wire format |

### Exporter Common Libraries

The `exporter_common/` directory provides reusable components for building exporters:

```
bridges/exporter_common/
├── include/
│   ├── transport_sink.hpp   # Abstract transport interface
│   ├── compressor.hpp       # Compression (zstd, none)
│   ├── wire_encoder.hpp     # DDS → Protobuf conversion
│   ├── batch_builder.hpp    # Signal/Event/Metric batching
│   └── mqtt_sink.hpp        # MQTT transport implementation
└── src/
    └── *.cpp
```

**Usage for new exporter (e.g., SOME/IP):**
```cpp
#include "batch_builder.hpp"
#include "compressor.hpp"
#include "transport_sink.hpp"

// Custom transport implementing TransportSink interface
class SomeipSink : public vep::exporter::TransportSink { ... };

// Reuse batching and compression
vep::exporter::SignalBatchBuilder batcher("source_id");
auto compressor = vep::exporter::create_compressor("zstd", 3);

// On DDS signal received:
batcher.add(signal);
if (batcher.size() >= 100) {
    auto batch = batcher.build();
    auto compressed = compressor->compress(batch);
    someip_sink->publish("signals", compressed);
}
```

## Dependencies

**Required (from sibling components):**
- `vep_idl` - DDS message types (from vep-schema)
- `vep_dds_common` - DDS wrappers (from vep-dds)
- `vssdag` - CAN→VSS transformation (from libvssdag)
- `vss::types` - VSS type definitions (from libvss-types)

**Optional:**
- `kuksa` - KUKSA client (from libkuksa-cpp) - enables kuksa_dds_bridge

**System packages:**
- CycloneDDS, libmosquitto, libzstd, protobuf, glog, yaml-cpp, nlohmann_json, gflags

## Code Conventions

- **Namespaces:** No project-wide namespace; use `vep::` for IDL types
- **Logging:** glog (`LOG(INFO)`, `LOG(ERROR)`, `VLOG(1)` for verbose)
- **CLI flags:** gflags for command-line parsing
- **Config files:** YAML via yaml-cpp
- **DDS patterns:** Use vep_dds_common wrappers (not raw CycloneDDS C API)

## Wire Protocol (transfer.proto)

The exporter uses a bandwidth-optimized protobuf format:
- Delta timestamps (base + per-signal deltas)
- Batched signals (multiple per message)
- Zstd compression (60-80% reduction)

See `proto/transfer.proto` for message definitions.

## Testing

```bash
# Run all vep-core tests
cd build/vep-core && ctest --output-on-failure

# Integration tests (may require Docker for KUKSA)
ctest -L integration --output-on-failure

# Skip Docker-dependent tests
ctest -E "reconnect" --output-on-failure
```

## Common Tasks

**Adding a new probe:**
1. Create directory under `probes/`
2. Add `main.cpp` with gflags CLI
3. Use vep_dds_common for DDS publishing
4. Add to `probes/CMakeLists.txt`

**Adding a new DDS message type:**
1. Define in `components/vep-schema/ifex/`
2. Regenerate: `cd components/vep-schema && ./generate-all.sh`
3. Add conversion in `bridges/vep_exporter/src/dds_proto_conversion.cpp`
4. Add to subscriber in `bridges/vep_exporter/src/subscriber.cpp`

**Modifying wire protocol:**
1. Edit `proto/transfer.proto`
2. Rebuild (CMake regenerates automatically)
3. Update `vep_mqtt_receiver` decoder if needed
