// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file can_simulator_main.cpp
/// @brief CAN Simulator - Generates simulated vehicle data, transforms via libvssdag, publishes to DDS
///
/// This simulator:
/// 1. Generates realistic vehicle CAN signals using VehicleModel
/// 2. Transforms them to VSS format using libvssdag (DAG processing, Lua transforms)
/// 3. Publishes VSS signals to DDS topic rt/vss/signals
///
/// Usage:
///   can_simulator --config config/model3_mappings_dag.yaml --scenario highway

#include "vehicle_model.hpp"
#include "common/dds_wrapper.hpp"
#include "common/qos_profiles.hpp"
#include "common/time_utils.hpp"
#include "telemetry.h"

#include <vssdag/signal_processor.h>
#include <vssdag/mapping_types.h>
#include <vss/types/types.hpp>

#include <glog/logging.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    LOG(INFO) << "Received signal " << signum << ", shutting down...";
    g_running = false;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "\n"
              << "Options:\n"
              << "  --config PATH     Signal mappings YAML file (default: config/model3_mappings_dag.yaml)\n"
              << "  --scenario NAME   Driving scenario: parked, city, highway, aggressive, charging\n"
              << "                    (default: city)\n"
              << "  --interval MS     Update interval in milliseconds (default: 50)\n"
              << "  --duration SEC    Run duration in seconds (0 = infinite, default: 0)\n"
              << "  --help            Show this help\n"
              << "\n"
              << "This simulator generates realistic vehicle signals, transforms them\n"
              << "via libvssdag to VSS format, and publishes to DDS topic rt/vss/signals.\n";
}

// Parse ValueType from string
vss::types::ValueType parse_datatype(const std::string& dtype) {
    if (dtype == "bool" || dtype == "boolean") return vss::types::ValueType::BOOL;
    if (dtype == "int8") return vss::types::ValueType::INT8;
    if (dtype == "int16") return vss::types::ValueType::INT16;
    if (dtype == "int32") return vss::types::ValueType::INT32;
    if (dtype == "int64") return vss::types::ValueType::INT64;
    if (dtype == "uint8") return vss::types::ValueType::UINT8;
    if (dtype == "uint16") return vss::types::ValueType::UINT16;
    if (dtype == "uint32") return vss::types::ValueType::UINT32;
    if (dtype == "uint64") return vss::types::ValueType::UINT64;
    if (dtype == "float") return vss::types::ValueType::FLOAT;
    if (dtype == "double") return vss::types::ValueType::DOUBLE;
    if (dtype == "string") return vss::types::ValueType::STRING;
    return vss::types::ValueType::UNSPECIFIED;
}

// Load signal mappings from YAML file
std::unordered_map<std::string, vssdag::SignalMapping> load_mappings(const std::string& yaml_path) {
    std::unordered_map<std::string, vssdag::SignalMapping> mappings;

    YAML::Node config = YAML::LoadFile(yaml_path);

    if (!config["mappings"]) {
        LOG(WARNING) << "No 'mappings' section in config";
        return mappings;
    }

    for (const auto& sig : config["mappings"]) {
        vssdag::SignalMapping mapping;

        std::string signal_name = sig["signal"].as<std::string>();

        // Data type
        if (sig["datatype"]) {
            std::string dtype = sig["datatype"].as<std::string>();
            mapping.datatype = parse_datatype(dtype);
        }

        // Source configuration
        if (sig["source"]) {
            auto source = sig["source"];
            mapping.source.type = source["type"].as<std::string>("dbc");
            mapping.source.name = source["name"].as<std::string>("");
        }

        // Dependencies
        if (sig["depends_on"]) {
            for (const auto& dep : sig["depends_on"]) {
                mapping.depends_on.push_back(dep.as<std::string>());
            }
        }

        // Transform
        if (sig["transform"]) {
            auto transform = sig["transform"];
            if (transform["code"]) {
                vssdag::CodeTransform code_transform;
                code_transform.expression = transform["code"].as<std::string>();
                mapping.transform = code_transform;
            } else if (transform["mapping"]) {
                vssdag::ValueMapping value_map;
                for (const auto& item : transform["mapping"]) {
                    std::string from = item["from"].as<std::string>();
                    // Handle different 'to' types
                    if (item["to"].IsScalar()) {
                        value_map.mappings[from] = item["to"].as<std::string>();
                    }
                }
                mapping.transform = value_map;
            }
        }

        // Throttling
        if (sig["interval_ms"]) {
            mapping.interval_ms = sig["interval_ms"].as<int>();
        }

        // Update trigger
        if (sig["update_trigger"]) {
            std::string trigger = sig["update_trigger"].as<std::string>();
            if (trigger == "periodic") {
                mapping.update_trigger = vssdag::UpdateTrigger::PERIODIC;
            } else if (trigger == "both") {
                mapping.update_trigger = vssdag::UpdateTrigger::BOTH;
            } else {
                mapping.update_trigger = vssdag::UpdateTrigger::ON_DEPENDENCY;
            }
        }

        mappings[signal_name] = std::move(mapping);
    }

    LOG(INFO) << "Loaded " << mappings.size() << " signal mappings from " << yaml_path;
    return mappings;
}

// Convert vss::types::SignalQuality to DDS Quality enum
telemetry_Quality convert_quality(vss::types::SignalQuality quality) {
    switch (quality) {
        case vss::types::SignalQuality::VALID:
            return telemetry_QUALITY_VALID;
        case vss::types::SignalQuality::INVALID:
            return telemetry_QUALITY_INVALID;
        case vss::types::SignalQuality::NOT_AVAILABLE:
        default:
            return telemetry_QUALITY_NOT_AVAILABLE;
    }
}

// Set DDS message value fields from vss::types::Value
bool set_value_fields(telemetry_vss_Signal& msg, const vss::types::Value& value,
                      std::string& string_buf) {
    // Set value in the nested value structure
    if (std::holds_alternative<bool>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_BOOL;
        msg.value.bool_value = std::get<bool>(value);
        return true;
    }
    if (std::holds_alternative<int8_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_INT8;
        msg.value.int8_value = static_cast<uint8_t>(std::get<int8_t>(value));
        return true;
    }
    if (std::holds_alternative<int16_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_INT16;
        msg.value.int16_value = std::get<int16_t>(value);
        return true;
    }
    if (std::holds_alternative<int32_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_INT32;
        msg.value.int32_value = std::get<int32_t>(value);
        return true;
    }
    if (std::holds_alternative<int64_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_INT64;
        msg.value.int64_value = std::get<int64_t>(value);
        return true;
    }
    if (std::holds_alternative<uint8_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_UINT8;
        msg.value.uint8_value = std::get<uint8_t>(value);
        return true;
    }
    if (std::holds_alternative<uint16_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_UINT16;
        msg.value.uint16_value = std::get<uint16_t>(value);
        return true;
    }
    if (std::holds_alternative<uint32_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_UINT32;
        msg.value.uint32_value = std::get<uint32_t>(value);
        return true;
    }
    if (std::holds_alternative<uint64_t>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_UINT64;
        msg.value.uint64_value = std::get<uint64_t>(value);
        return true;
    }
    if (std::holds_alternative<float>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_FLOAT;
        msg.value.float_value = std::get<float>(value);
        return true;
    }
    if (std::holds_alternative<double>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_DOUBLE;
        msg.value.double_value = std::get<double>(value);
        return true;
    }
    if (std::holds_alternative<std::string>(value)) {
        msg.value.type = telemetry_types_VALUE_TYPE_STRING;
        string_buf = std::get<std::string>(value);
        msg.value.string_value = const_cast<char*>(string_buf.c_str());
        return true;
    }

    return false;
}

simulator::VehicleModel::Scenario parse_scenario(const std::string& name) {
    if (name == "parked") return simulator::VehicleModel::Scenario::PARKED;
    if (name == "city") return simulator::VehicleModel::Scenario::CITY_DRIVING;
    if (name == "highway") return simulator::VehicleModel::Scenario::HIGHWAY_DRIVING;
    if (name == "aggressive") return simulator::VehicleModel::Scenario::AGGRESSIVE_DRIVING;
    if (name == "charging") return simulator::VehicleModel::Scenario::CHARGING;
    LOG(WARNING) << "Unknown scenario '" << name << "', defaulting to city";
    return simulator::VehicleModel::Scenario::CITY_DRIVING;
}

}  // namespace

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    FLAGS_colorlogtostderr = true;

    // Parse arguments
    std::string config_path = "config/model3_mappings_dag.yaml";
    std::string scenario_name = "city";
    int interval_ms = 50;
    int duration_sec = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        } else if (arg == "--scenario" && i + 1 < argc) {
            scenario_name = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            interval_ms = std::stoi(argv[++i]);
        } else if (arg == "--duration" && i + 1 < argc) {
            duration_sec = std::stoi(argv[++i]);
        }
    }

    LOG(INFO) << "=== CAN Simulator Starting ===";
    LOG(INFO) << "Config: " << config_path;
    LOG(INFO) << "Scenario: " << scenario_name;
    LOG(INFO) << "Interval: " << interval_ms << "ms";

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Load signal mappings
        auto mappings = load_mappings(config_path);
        if (mappings.empty()) {
            LOG(ERROR) << "No signal mappings loaded";
            return 1;
        }

        // Initialize signal processor DAG
        vssdag::SignalProcessorDAG processor;
        if (!processor.initialize(mappings)) {
            LOG(ERROR) << "Failed to initialize signal processor";
            return 1;
        }
        LOG(INFO) << "Signal processor initialized with " << mappings.size() << " mappings";

        // Initialize vehicle model
        simulator::VehicleModel vehicle;
        vehicle.set_scenario(parse_scenario(scenario_name));

        // Create DDS participant and writer
        dds::Participant participant(DDS_DOMAIN_DEFAULT);
        auto qos = dds::qos_profiles::reliable_standard(100);
        dds::Topic topic(participant, &telemetry_vss_Signal_desc, "rt/vss/signals", qos.get());
        dds::Writer writer(participant, topic, qos.get());

        LOG(INFO) << "DDS writer ready on topic rt/vss/signals";
        LOG(INFO) << "Simulator running. Press Ctrl+C to stop.";

        uint32_t seq = 0;
        uint64_t signals_published = 0;
        std::string source_id = "can_simulator";
        std::string correlation_id = "";

        auto start_time = std::chrono::steady_clock::now();
        auto last_status = start_time;

        while (g_running) {
            auto loop_start = std::chrono::steady_clock::now();

            // Check duration
            if (duration_sec > 0) {
                auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                    loop_start - start_time).count();
                if (elapsed >= duration_sec) {
                    LOG(INFO) << "Duration reached, stopping";
                    break;
                }
            }

            // Update vehicle model
            vehicle.update(std::chrono::milliseconds(interval_ms));

            // Convert vehicle signals to SignalUpdate format
            std::vector<vssdag::SignalUpdate> updates;
            for (const auto& [signal_name, value] : vehicle.get_signals()) {
                vssdag::SignalUpdate update;
                update.signal_name = signal_name;
                update.value = value;
                update.timestamp = std::chrono::steady_clock::now();
                update.status = vss::types::SignalQuality::VALID;
                updates.push_back(std::move(update));
            }

            // Process through DAG (transforms, derived signals)
            auto vss_signals = processor.process_signal_updates(updates);

            // String buffers for DDS messages
            std::vector<std::string> path_buffers(vss_signals.size());
            std::vector<std::string> string_value_buffers(vss_signals.size());

            // Publish each output signal to DDS
            for (size_t i = 0; i < vss_signals.size(); ++i) {
                const auto& sig = vss_signals[i];

                // Only publish valid signals
                if (sig.qualified_value.quality != vss::types::SignalQuality::VALID) {
                    continue;
                }

                telemetry_vss_Signal msg = {};

                // Path
                path_buffers[i] = sig.path;
                msg.path = const_cast<char*>(path_buffers[i].c_str());

                // Header
                msg.header.source_id = const_cast<char*>(source_id.c_str());
                msg.header.timestamp_ns = utils::now_ns();
                msg.header.seq_num = seq++;
                msg.header.correlation_id = const_cast<char*>(correlation_id.c_str());

                // Quality
                msg.quality = convert_quality(sig.qualified_value.quality);

                // Value
                if (!set_value_fields(msg, sig.qualified_value.value, string_value_buffers[i])) {
                    continue;  // Unsupported type
                }

                writer.write(msg);
                ++signals_published;
            }

            // Status logging every 5 seconds
            auto now = std::chrono::steady_clock::now();
            if (now - last_status >= std::chrono::seconds(5)) {
                LOG(INFO) << "Status: speed=" << vehicle.speed() << " km/h, "
                          << "soc=" << vehicle.soc() << "%, "
                          << "gear=" << vehicle.gear() << ", "
                          << "signals_published=" << signals_published;
                last_status = now;
            }

            // Sleep for remaining interval
            auto elapsed = std::chrono::steady_clock::now() - loop_start;
            auto sleep_time = std::chrono::milliseconds(interval_ms) - elapsed;
            if (sleep_time > std::chrono::milliseconds(0)) {
                std::this_thread::sleep_for(sleep_time);
            }
        }

        LOG(INFO) << "Simulator stopped. Total signals published: " << signals_published;

    } catch (const YAML::Exception& e) {
        LOG(FATAL) << "YAML error: " << e.what();
        return 1;
    } catch (const dds::Error& e) {
        LOG(FATAL) << "DDS error: " << e.what();
        return 1;
    } catch (const std::exception& e) {
        LOG(FATAL) << "Error: " << e.what();
        return 1;
    }

    google::ShutdownGoogleLogging();
    return 0;
}
