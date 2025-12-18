// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief VEP MQTT Receiver - Receives and decodes compressed MQTT messages
///
/// This tool simulates a cloud backend receiving vehicle telemetry data.
/// It subscribes to MQTT topics, decompresses with zstd, decodes protobuf,
/// and displays the data in JSON format for debugging.
///
/// Uses exporter_common libraries for decompression and decoding.
///
/// Usage:
///   vep_mqtt_receiver [--broker HOST] [--port PORT] [--topic PREFIX]

#include "compressor.hpp"
#include "wire_decoder.hpp"

#include <glog/logging.h>
#include <mosquitto.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using json = nlohmann::json;
using namespace vep::exporter;

namespace {

std::atomic<bool> g_running{true};
std::unique_ptr<Decompressor> g_decompressor;

void signal_handler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "VEP MQTT Receiver - Receives and decodes vehicle telemetry\n"
              << "\n"
              << "Options:\n"
              << "  --broker HOST     MQTT broker host (default: localhost)\n"
              << "  --port PORT       MQTT broker port (default: 1883)\n"
              << "  --topic PREFIX    Topic prefix to subscribe (default: v1/telemetry/#)\n"
              << "  --json            Output raw JSON format\n"
              << "  --verbose         Verbose output including metadata\n"
              << "  --no-compression  Expect uncompressed messages\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << prog << " --broker localhost --port 1883\n"
              << "\n";
}

struct Config {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string topic_subscribe = "v1/telemetry/#";
    bool json_output = false;
    bool verbose = false;
    CompressorType compression = CompressorType::ZSTD;
};

Config g_config;

// =============================================================================
// JSON Conversion Helpers
// =============================================================================

json value_to_json(const DecodedValue& value);

json struct_to_json(const DecodedStruct& s) {
    json j;
    j["_type"] = s.type_name;
    for (const auto& field : s.fields) {
        j[field.name] = value_to_json(field.value);
    }
    return j;
}

json value_to_json(const DecodedValue& value) {
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, std::monostate>) {
            return nullptr;
        } else if constexpr (std::is_same_v<T, bool>) {
            return v;
        } else if constexpr (std::is_same_v<T, int8_t> || std::is_same_v<T, int16_t> ||
                             std::is_same_v<T, int32_t> || std::is_same_v<T, int64_t>) {
            return v;
        } else if constexpr (std::is_same_v<T, uint8_t> || std::is_same_v<T, uint16_t> ||
                             std::is_same_v<T, uint32_t> || std::is_same_v<T, uint64_t>) {
            return v;
        } else if constexpr (std::is_same_v<T, float> || std::is_same_v<T, double>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::string>) {
            return v;
        } else if constexpr (std::is_same_v<T, std::vector<bool>>) {
            json arr = json::array();
            for (bool b : v) arr.push_back(b);
            return arr;
        } else if constexpr (std::is_same_v<T, std::vector<int8_t>> ||
                             std::is_same_v<T, std::vector<int16_t>> ||
                             std::is_same_v<T, std::vector<int32_t>> ||
                             std::is_same_v<T, std::vector<int64_t>> ||
                             std::is_same_v<T, std::vector<uint8_t>> ||
                             std::is_same_v<T, std::vector<uint16_t>> ||
                             std::is_same_v<T, std::vector<uint32_t>> ||
                             std::is_same_v<T, std::vector<uint64_t>> ||
                             std::is_same_v<T, std::vector<float>> ||
                             std::is_same_v<T, std::vector<double>>) {
            json arr = json::array();
            for (const auto& x : v) arr.push_back(x);
            return arr;
        } else if constexpr (std::is_same_v<T, std::vector<std::string>>) {
            json arr = json::array();
            for (const auto& s : v) arr.push_back(s);
            return arr;
        } else if constexpr (std::is_same_v<T, DecodedStruct>) {
            return struct_to_json(v);
        } else if constexpr (std::is_same_v<T, std::vector<DecodedStruct>>) {
            json arr = json::array();
            for (const auto& s : v) arr.push_back(struct_to_json(s));
            return arr;
        } else {
            return nullptr;
        }
    }, value);
}

const char* severity_to_string(int32_t severity) {
    switch (severity) {
        case 0: return "INFO";
        case 1: return "WARNING";
        case 2: return "ERROR";
        case 3: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

// =============================================================================
// Batch Processing
// =============================================================================

void process_signal_batch(const std::vector<uint8_t>& data) {
    auto batch = decode_signal_batch(data);
    if (!batch) {
        LOG(ERROR) << "Failed to parse SignalBatch";
        return;
    }

    // Get current time for delay calculation
    auto now = std::chrono::system_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Calculate min/max timestamps in batch
    int64_t min_ts = INT64_MAX;
    int64_t max_ts = 0;
    for (const auto& sig : batch->signals) {
        if (sig.timestamp_ms < min_ts) min_ts = sig.timestamp_ms;
        if (sig.timestamp_ms > max_ts) max_ts = sig.timestamp_ms;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "signal_batch";
        j["source_id"] = batch->source_id;
        j["sequence"] = batch->sequence;
        j["base_timestamp_ms"] = batch->base_timestamp_ms;
        j["received_at_ms"] = now_ms;
        j["first_signal_delay_ms"] = static_cast<int64_t>(now_ms) - min_ts;
        j["last_signal_delay_ms"] = static_cast<int64_t>(now_ms) - max_ts;
        j["signals"] = json::array();

        for (const auto& sig : batch->signals) {
            json js;
            js["path"] = sig.path;
            js["timestamp_ms"] = sig.timestamp_ms;
            js["quality"] = quality_to_string(sig.quality);
            js["value"] = value_to_json(sig.value);
            j["signals"].push_back(js);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Signal Batch [" << batch->base_timestamp_ms << " ms] ===\n";
        std::cout << "Source: " << batch->source_id
                  << " | Seq: " << batch->sequence
                  << " | Signals: " << batch->signals.size()
                  << " | Span: " << (max_ts - min_ts) << " ms\n";
        std::cout << "Delay: first=" << (static_cast<int64_t>(now_ms) - min_ts) << " ms"
                  << ", last=" << (static_cast<int64_t>(now_ms) - max_ts) << " ms\n";

        for (const auto& sig : batch->signals) {
            std::cout << "  [" << sig.timestamp_ms << "] " << sig.path
                      << " = " << value_to_string(sig.value);

            if (sig.quality != DecodedQuality::VALID) {
                std::cout << " [" << quality_to_string(sig.quality) << "]";
            }
            std::cout << "\n";
        }
    }
}

void process_event_batch(const std::vector<uint8_t>& data) {
    auto batch = decode_event_batch(data);
    if (!batch) {
        LOG(ERROR) << "Failed to parse EventBatch";
        return;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "event_batch";
        j["source_id"] = batch->source_id;
        j["sequence"] = batch->sequence;
        j["events"] = json::array();

        for (const auto& evt : batch->events) {
            json je;
            je["event_id"] = evt.event_id;
            je["timestamp_ms"] = evt.timestamp_ms;
            je["category"] = evt.category;
            je["event_type"] = evt.event_type;
            je["severity"] = severity_to_string(evt.severity);
            if (!evt.attributes.empty()) {
                je["attributes"] = evt.attributes;
            }
            j["events"].push_back(je);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Event Batch ===\n";
        if (g_config.verbose) {
            std::cout << "Source: " << batch->source_id
                      << " | Seq: " << batch->sequence << "\n";
        }

        for (const auto& evt : batch->events) {
            std::cout << "  [" << severity_to_string(evt.severity) << "] "
                      << evt.category << "/" << evt.event_type
                      << " (" << evt.event_id << ")\n";
        }
    }
}

void process_metrics_batch(const std::vector<uint8_t>& data) {
    auto batch = decode_metrics_batch(data);
    if (!batch) {
        LOG(ERROR) << "Failed to parse MetricsBatch";
        return;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "metrics_batch";
        j["source_id"] = batch->source_id;
        j["sequence"] = batch->sequence;
        j["metrics"] = json::array();

        for (const auto& m : batch->metrics) {
            json jm;
            jm["name"] = m.name;
            jm["type"] = metric_type_to_string(m.type);
            jm["value"] = m.value;

            if (m.type == MetricType::HISTOGRAM) {
                jm["sample_count"] = m.sample_count;
                jm["sample_sum"] = m.sample_sum;
            }

            if (!m.labels.empty()) {
                jm["labels"] = m.labels;
            }
            j["metrics"].push_back(jm);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Metrics Batch [" << batch->source_id << "] ===\n";
        if (g_config.verbose) {
            std::cout << "Seq: " << batch->sequence << "\n";
        }

        for (const auto& m : batch->metrics) {
            // Build labels string
            std::string labels;
            if (!m.labels.empty()) {
                labels = "{";
                bool first = true;
                for (const auto& [k, v] : m.labels) {
                    if (!first) labels += ",";
                    labels += k + "=" + v;
                    first = false;
                }
                labels += "}";
            }

            std::cout << "  [" << metric_type_to_string(m.type) << "] "
                      << m.name << labels << " = " << m.value;

            if (m.type == MetricType::HISTOGRAM) {
                std::cout << " (count=" << m.sample_count << " sum=" << m.sample_sum << ")";
            }
            std::cout << "\n";
        }
    }
}

void process_log_batch(const std::vector<uint8_t>& data) {
    auto batch = decode_log_batch(data);
    if (!batch) {
        LOG(ERROR) << "Failed to parse LogBatch";
        return;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "log_batch";
        j["source_id"] = batch->source_id;
        j["sequence"] = batch->sequence;
        j["logs"] = json::array();

        for (const auto& log : batch->entries) {
            json jl;
            jl["timestamp_ms"] = log.timestamp_ms;
            jl["level"] = log_level_to_string(log.level);
            jl["component"] = log.component;
            jl["message"] = log.message;

            if (!log.attributes.empty()) {
                jl["attributes"] = log.attributes;
            }
            if (!log.trace_id.empty()) {
                jl["trace_id"] = log.trace_id;
            }
            if (!log.span_id.empty()) {
                jl["span_id"] = log.span_id;
            }
            j["logs"].push_back(jl);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Log Batch ===\n";
        if (g_config.verbose) {
            std::cout << "Source: " << batch->source_id
                      << " | Seq: " << batch->sequence << "\n";
        }

        for (const auto& log : batch->entries) {
            std::cout << "  [" << log.timestamp_ms << "] ["
                      << log_level_to_string(log.level) << "] "
                      << log.component << ": " << log.message << "\n";
        }
    }
}

// =============================================================================
// MQTT Callbacks
// =============================================================================

void on_message(struct mosquitto* /*mosq*/, void* /*obj*/,
                const struct mosquitto_message* msg) {
    std::string topic(msg->topic);

    LOG(INFO) << "Received message on " << topic
              << " (" << msg->payloadlen << " bytes"
              << (g_config.compression == CompressorType::ZSTD ? " compressed" : "") << ")";

    // Decompress if needed
    std::vector<uint8_t> decompressed;
    if (g_config.compression == CompressorType::ZSTD) {
        std::vector<uint8_t> compressed(
            static_cast<uint8_t*>(msg->payload),
            static_cast<uint8_t*>(msg->payload) + msg->payloadlen);

        decompressed = g_decompressor->decompress(compressed);
        if (decompressed.empty() && msg->payloadlen > 0) {
            LOG(ERROR) << "Decompression failed";
            return;
        }

        LOG(INFO) << "Decompressed to " << decompressed.size() << " bytes ("
                  << std::fixed << std::setprecision(1)
                  << (100.0 * msg->payloadlen / decompressed.size()) << "% of original)";
    } else {
        decompressed.assign(
            static_cast<uint8_t*>(msg->payload),
            static_cast<uint8_t*>(msg->payload) + msg->payloadlen);
    }

    // Determine type from topic
    if (topic.find("/signals") != std::string::npos) {
        process_signal_batch(decompressed);
    } else if (topic.find("/events") != std::string::npos) {
        process_event_batch(decompressed);
    } else if (topic.find("/metrics") != std::string::npos) {
        process_metrics_batch(decompressed);
    } else if (topic.find("/logs") != std::string::npos) {
        process_log_batch(decompressed);
    } else {
        LOG(WARNING) << "Unknown topic type: " << topic;
    }
}

void on_connect(struct mosquitto* mosq, void* /*obj*/, int rc) {
    if (rc == 0) {
        LOG(INFO) << "Connected to MQTT broker";
        mosquitto_subscribe(mosq, nullptr, g_config.topic_subscribe.c_str(), 0);
        LOG(INFO) << "Subscribed to: " << g_config.topic_subscribe;
    } else {
        LOG(ERROR) << "Connection failed: " << mosquitto_connack_string(rc);
    }
}

void on_disconnect(struct mosquitto* /*mosq*/, void* /*obj*/, int rc) {
    if (rc != 0) {
        LOG(WARNING) << "Unexpected disconnect: " << rc;
    }
}

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--broker" && i + 1 < argc) {
            config.broker_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.broker_port = std::stoi(argv[++i]);
        } else if (arg == "--topic" && i + 1 < argc) {
            config.topic_subscribe = argv[++i];
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg == "--verbose") {
            config.verbose = true;
        } else if (arg == "--no-compression") {
            config.compression = CompressorType::NONE;
        } else {
            LOG(WARNING) << "Unknown argument: " << arg;
        }
    }

    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Initialize logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    LOG(INFO) << "VEP MQTT Receiver starting...";

    // Parse configuration
    g_config = parse_args(argc, argv);
    LOG(INFO) << "MQTT Broker: " << g_config.broker_host << ":" << g_config.broker_port;
    LOG(INFO) << "Subscribe topic: " << g_config.topic_subscribe;
    LOG(INFO) << "Compression: " << to_string(g_config.compression);

    // Create decompressor
    g_decompressor = create_decompressor(g_config.compression);
    if (!g_decompressor) {
        LOG(ERROR) << "Failed to create decompressor";
        return 1;
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize Mosquitto
    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("vep_mqtt_receiver", true, nullptr);
    if (!mosq) {
        LOG(ERROR) << "Failed to create Mosquitto client";
        return 1;
    }

    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);

    // Connect with retry
    LOG(INFO) << "Connecting to broker...";
    int rc;
    while (g_running) {
        rc = mosquitto_connect(mosq, g_config.broker_host.c_str(), g_config.broker_port, 60);
        if (rc == MOSQ_ERR_SUCCESS) {
            break;
        }
        LOG(INFO) << "Waiting for broker at " << g_config.broker_host << ":" << g_config.broker_port << "...";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (!g_running) {
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 0;
    }

    LOG(INFO) << "VEP MQTT Receiver running. Press Ctrl+C to stop.";

    // Event loop
    bool was_connected = true;
    while (g_running) {
        rc = mosquitto_loop(mosq, 100, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            if (was_connected) {
                LOG(WARNING) << "Connection lost: " << mosquitto_strerror(rc);
                was_connected = false;
            }
            // Try to reconnect with backoff
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (mosquitto_reconnect(mosq) == MOSQ_ERR_SUCCESS) {
                LOG(INFO) << "Reconnected to broker";
                was_connected = true;
            }
        }
    }

    // Cleanup
    LOG(INFO) << "Shutting down...";

    // Log decompression stats
    auto stats = g_decompressor->stats();
    if (stats.operations > 0) {
        LOG(INFO) << "Decompression stats: " << stats.operations << " operations, "
                  << stats.bytes_before << " bytes in, " << stats.bytes_after << " bytes out "
                  << "(expansion ratio: " << std::fixed << std::setprecision(2)
                  << stats.ratio() << "x)";
        if (stats.errors > 0) {
            LOG(WARNING) << "Decompression errors: " << stats.errors;
        }
    }

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    LOG(INFO) << "VEP MQTT Receiver stopped.";
    return 0;
}
