# vep_can_actuator

DDS to CAN actuator bridge - subscribes to DDS actuator topics and writes encoded values to SocketCAN interfaces.

## Overview

`vep_can_actuator` bridges the gap between the DDS-based Vehicle Edge Platform and CAN-based vehicle actuators. It:

1. **Parses** a `vss_dbc.json` configuration file defining VSS actuator paths and their CAN signal encodings
2. **Subscribes** to DDS topic `rt/vss/actuators/target` for actuator commands
3. **Encodes** VSS values to CAN frame bytes using the configured transforms
4. **Writes** CAN frames to a SocketCAN interface (e.g., `vcan0`, `can0`)

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│  DDS Domain                                                      │
│                                                                  │
│  [Application] ──publish──▶ rt/vss/actuators/target             │
│                                    │                             │
│                                    ▼                             │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │  vep_can_actuator                                        │   │
│  │                                                          │   │
│  │  ┌──────────────┐   ┌─────────────┐   ┌──────────────┐  │   │
│  │  │ VssDbcParser │──▶│ CANEncoder  │──▶│ SocketCAN    │  │   │
│  │  │ (JSON)       │   │ (VSS→CAN)   │   │ Writer       │  │   │
│  │  └──────────────┘   └─────────────┘   └──────────────┘  │   │
│  │         ▲                  ▲                  │          │   │
│  │         │                  │                  │          │   │
│  │   vss_dbc.json       vep_VssSignal           │          │   │
│  │                                               ▼          │   │
│  └───────────────────────────────────────► CAN frames      │   │
│                                                              │   │
└──────────────────────────────────────────────────────────────┘   │
                                               │
                                               ▼
                                        SocketCAN (vcan0)
                                               │
                                               ▼
                                      Vehicle ECU / Actuators
```

## Configuration

### vss_dbc.json Format

The configuration file follows the VSS tree structure with `vss2dbc` nodes defining CAN signal mappings:

```json
{
  "Vehicle": {
    "type": "branch",
    "children": {
      "Cabin": {
        "type": "branch",
        "children": {
          "HVAC": {
            "type": "branch",
            "children": {
              "IsAirConditioningActive": {
                "datatype": "int8",
                "type": "actuator",
                "vss2dbc": {
                  "message": {
                    "name": "HVAC_Control",
                    "canid": "0x1AFFCB02",
                    "length_in_bytes": 8,
                    "cycle_time_ms": 1000
                  },
                  "signal": {
                    "name": "AC_Active",
                    "bitposition": {
                      "start": 0,
                      "length": 8
                    },
                    "transform": {
                      "offset": 10,
                      "factor": 5
                    },
                    "min": -100,
                    "max": 100
                  }
                }
              },
              "AirDistribution": {
                "datatype": "string",
                "type": "actuator",
                "vss2dbc": {
                  "message": {
                    "name": "AirDistributionControl",
                    "canid": "0x1AFFCB02",
                    "length_in_bytes": 8
                  },
                  "signal": {
                    "name": "Air_Distribution_Mode",
                    "bitposition": {
                      "start": 30,
                      "length": 2
                    },
                    "transform": {
                      "mapping": {
                        "UP": 0,
                        "MIDDLE": 1,
                        "DOWN": 2
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
```

### Configuration Fields

#### Message Section
| Field | Type | Description |
|-------|------|-------------|
| `name` | string | CAN message name (informational) |
| `canid` | string | CAN arbitration ID (hex format, e.g., "0x1AFFCB02") |
| `length_in_bytes` | int | CAN frame data length (1-8 for classic CAN) |
| `cycle_time_ms` | int | Cycle time in milliseconds (informational) |

#### Signal Section
| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Signal name (informational) |
| `bitposition.start` | int | Start bit position in the CAN frame |
| `bitposition.length` | int | Signal length in bits |
| `transform.offset` | float | Offset for numeric transform |
| `transform.factor` | float | Factor for numeric transform |
| `transform.mapping` | object | String-to-int enum mapping |
| `min` | float | Minimum allowed value |
| `max` | float | Maximum allowed value |

### Signal Encoding

#### Numeric Signals
For numeric values, the transform is:
```
raw_value = (vss_value - offset) / factor
```

Example: With `offset=10, factor=5`, a VSS value of 35 becomes raw value 5.

#### String Enum Signals
For string values, the mapping table converts strings to integers:
```
"UP" → 0, "MIDDLE" → 1, "DOWN" → 2
```

## Usage

### Command Line Options

```
vep_can_actuator --config <path> [options]

Required:
  --config <path>       Path to vss_dbc.json configuration file

Optional:
  --interface <name>    CAN interface name (default: vcan0)
  --topic <name>        DDS topic to subscribe to (default: rt/vss/actuators/target)
  --help                Show help message
```

### Running the Bridge

```bash
# 1. Setup virtual CAN interface (for testing)
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# 2. Start the actuator bridge
./build/vep-core/bridges/vep_can_actuator/vep_can_actuator \
    --config /path/to/vss_dbc.json \
    --interface vcan0

# 3. Monitor CAN traffic (in another terminal)
candump vcan0
```

### Testing with actuator_test_publisher

A test publisher tool is included for sending DDS actuator commands:

```bash
# Send numeric value
./build/vep-core/bridges/vep_can_actuator/actuator_test_publisher \
    --path "Vehicle.Cabin.HVAC.IsAirConditioningActive" \
    --value 50

# Send string enum value
./build/vep-core/bridges/vep_can_actuator/actuator_test_publisher \
    --path "Vehicle.Cabin.HVAC.AirDistribution" \
    --value MIDDLE

# Continuous publishing (10 messages, 500ms interval)
./build/vep-core/bridges/vep_can_actuator/actuator_test_publisher \
    --path "Vehicle.Cabin.HVAC.IsAirConditioningActive" \
    --value 75 \
    --count 10 \
    --interval_ms 500
```

## Building

The component is built automatically as part of the workspace:

```bash
# From workspace root
./build-all.sh           # Release build
./build-all.sh --debug   # Debug build

# Or build only this component
cmake --build build --target vep_can_actuator actuator_test_publisher
```

### Build Artifacts

| Binary | Description |
|--------|-------------|
| `vep_can_actuator` | Main actuator bridge executable |
| `actuator_test_publisher` | Test tool for publishing DDS actuator commands |
| `libvep_can_actuator_lib.a` | Static library with parser, encoder, and writer |

## Components

### Source Files

| File | Description |
|------|-------------|
| `vss_dbc_parser.hpp/cpp` | Parses vss_dbc.json and builds actuator→CAN mappings |
| `can_encoder.hpp/cpp` | Encodes VSS values to CAN frame bytes |
| `can_writer.hpp/cpp` | SocketCAN writer for sending CAN frames |
| `main.cpp` | Main executable with DDS subscription loop |
| `tools/actuator_test_publisher.cpp` | Test publisher for DDS actuator signals |

### Library API

```cpp
#include "vss_dbc_parser.hpp"
#include "can_encoder.hpp"
#include "can_writer.hpp"

// Parse configuration
vep::VssDbcParser parser;
parser.load("vss_dbc.json");

// Find mapping for an actuator
const vep::CANSignalMapping* mapping = parser.find_mapping("Vehicle.Cabin.HVAC.IsAirConditioningActive");

// Encode value to CAN frame
vep::CANEncoder encoder;
std::vector<uint8_t> frame_data(mapping->message_length, 0);
encoder.encode_signal(*mapping, vss_value, frame_data);

// Write to CAN
vep::SocketCANWriter writer;
writer.open("vcan0");
writer.write(mapping->can_id, frame_data);
```

## DDS Integration

### Topic Structure

| Topic | Type | Description |
|-------|------|-------------|
| `rt/vss/actuators/target` | `vep_VssSignal` | Actuator setpoint commands |

### Message Format

The bridge subscribes to `vep_VssSignal` messages:

```cpp
struct vep_VssSignal {
    char* path;           // VSS path (e.g., "Vehicle.Cabin.HVAC.IsAirConditioningActive")
    vep_Header header;    // source_id, timestamp_ns, seq_num
    vep_VssQuality quality;  // VALID, INVALID, NOT_AVAILABLE
    vep_VssValue value;   // Typed value (int, float, string, etc.)
};
```

Only signals with `quality == VALID` are processed.

## Testing

### Unit Tests

Unit tests are built when `VEP_BUILD_TESTS=ON`:

```bash
./build_all_tests.sh
ctest --test-dir build -R "vss_dbc_parser|can_encoder" --output-on-failure
```

### Integration Testing

```bash
# Terminal 1: Monitor CAN
candump vcan0

# Terminal 2: Run bridge
./build/vep-core/bridges/vep_can_actuator/vep_can_actuator \
    --config tests/test_vss_dbc.json \
    --interface vcan0

# Terminal 3: Publish test signals
./build/vep-core/bridges/vep_can_actuator/actuator_test_publisher \
    --path "Vehicle.Cabin.HVAC.IsAirConditioningActive" \
    --value 50

# Expected output in candump:
#   vcan0  1AFFCB02   [8]  08 00 00 00 00 00 00 00
#   (value 50 with offset=10, factor=5 → raw=8)
```

## Troubleshooting

### Common Issues

**CAN interface not found:**
```
Failed to get interface index for vcan0: No such device
```
Solution: Create the virtual CAN interface:
```bash
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0
```

**No mapping found for actuator:**
```
No mapping found for: Vehicle.Some.Path
```
Solution: Ensure the path exists in vss_dbc.json with `type: "actuator"` and a valid `vss2dbc` section.

**Permission denied on CAN socket:**
```
Failed to create CAN socket: Operation not permitted
```
Solution: Run with appropriate permissions or add user to `can` group.

## Related Components

| Component | Description |
|-----------|-------------|
| `vep_can_probe` | Reverse direction: CAN → DDS (reads CAN, publishes VSS signals) |
| `kuksa_dds_bridge` | Bridges DDS signals to KUKSA databroker |
| `rt_dds_bridge` | DDS to RT transport bridge |

## License

Apache-2.0 - See [LICENSE](../../../../LICENSE) for details.
