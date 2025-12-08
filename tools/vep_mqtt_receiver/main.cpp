// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Cloud Backend Simulator - Receives and decodes compressed MQTT messages
///
/// This tool simulates a cloud backend receiving vehicle telemetry data.
/// It subscribes to MQTT topics, decompresses with zstd, decodes protobuf,
/// and displays the data in JSON format for debugging.
///
/// Usage:
///   cloud_backend_sim [--broker HOST] [--port PORT] [--topic PREFIX]

#include "transfer.pb.h"

#include <glog/logging.h>
#include <mosquitto.h>
#include <nlohmann/json.hpp>
#include <zstd.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using json = nlohmann::json;

namespace {

// Forward declarations for recursive struct conversion
json struct_value_to_json(const vep::transfer::StructValue& sv);
json struct_field_to_json(const vep::transfer::StructField& field);

json struct_value_to_json(const vep::transfer::StructValue& sv) {
    json j;
    j["_type"] = sv.type_name();
    for (const auto& field : sv.fields()) {
        j[field.name()] = struct_field_to_json(field);
    }
    return j;
}

json struct_field_to_json(const vep::transfer::StructField& field) {
    switch (field.value_case()) {
        case vep::transfer::StructField::kBoolVal:
            return field.bool_val();
        case vep::transfer::StructField::kInt32Val:
            return field.int32_val();
        case vep::transfer::StructField::kInt64Val:
            return field.int64_val();
        case vep::transfer::StructField::kUint32Val:
            return field.uint32_val();
        case vep::transfer::StructField::kUint64Val:
            return field.uint64_val();
        case vep::transfer::StructField::kFloatVal:
            return field.float_val();
        case vep::transfer::StructField::kDoubleVal:
            return field.double_val();
        case vep::transfer::StructField::kStringVal:
            return field.string_val();
        case vep::transfer::StructField::kBoolArray: {
            json arr = json::array();
            for (bool v : field.bool_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kInt32Array: {
            json arr = json::array();
            for (int32_t v : field.int32_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kInt64Array: {
            json arr = json::array();
            for (int64_t v : field.int64_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kFloatArray: {
            json arr = json::array();
            for (float v : field.float_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kDoubleArray: {
            json arr = json::array();
            for (double v : field.double_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kStringArray: {
            json arr = json::array();
            for (const auto& v : field.string_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::StructField::kStructVal:
            return struct_value_to_json(field.struct_val());
        case vep::transfer::StructField::kStructArray: {
            json arr = json::array();
            for (const auto& sv : field.struct_array().values()) {
                arr.push_back(struct_value_to_json(sv));
            }
            return arr;
        }
        default:
            return nullptr;
    }
}

json signal_value_to_json(const vep::transfer::Signal& sig) {
    switch (sig.value_case()) {
        case vep::transfer::Signal::kBoolVal:
            return sig.bool_val();
        case vep::transfer::Signal::kInt32Val:
            return sig.int32_val();
        case vep::transfer::Signal::kInt64Val:
            return sig.int64_val();
        case vep::transfer::Signal::kUint32Val:
            return sig.uint32_val();
        case vep::transfer::Signal::kUint64Val:
            return sig.uint64_val();
        case vep::transfer::Signal::kFloatVal:
            return sig.float_val();
        case vep::transfer::Signal::kDoubleVal:
            return sig.double_val();
        case vep::transfer::Signal::kStringVal:
            return sig.string_val();
        case vep::transfer::Signal::kBoolArray: {
            json arr = json::array();
            for (bool v : sig.bool_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kInt32Array: {
            json arr = json::array();
            for (int32_t v : sig.int32_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kInt64Array: {
            json arr = json::array();
            for (int64_t v : sig.int64_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kUint32Array: {
            json arr = json::array();
            for (uint32_t v : sig.uint32_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kUint64Array: {
            json arr = json::array();
            for (uint64_t v : sig.uint64_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kFloatArray: {
            json arr = json::array();
            for (float v : sig.float_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kDoubleArray: {
            json arr = json::array();
            for (double v : sig.double_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kStringArray: {
            json arr = json::array();
            for (const auto& v : sig.string_array().values()) arr.push_back(v);
            return arr;
        }
        case vep::transfer::Signal::kStructVal:
            return struct_value_to_json(sig.struct_val());
        case vep::transfer::Signal::kStructArray: {
            json arr = json::array();
            for (const auto& sv : sig.struct_array().values()) {
                arr.push_back(struct_value_to_json(sv));
            }
            return arr;
        }
        default:
            return nullptr;
    }
}

std::string signal_value_to_string(const vep::transfer::Signal& sig) {
    json j = signal_value_to_json(sig);
    if (j.is_null()) return "(empty)";
    if (j.is_string()) return "\"" + j.get<std::string>() + "\"";
    return j.dump();
}

std::atomic<bool> g_running{true};
ZSTD_DCtx* g_zstd_dctx = nullptr;

void signal_handler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Cloud Backend Simulator - Receives and decodes vehicle telemetry\n"
              << "\n"
              << "Options:\n"
              << "  --broker HOST     MQTT broker host (default: localhost)\n"
              << "  --port PORT       MQTT broker port (default: 1883)\n"
              << "  --topic PREFIX    Topic prefix to subscribe (default: v1/telemetry/#)\n"
              << "  --json            Output raw JSON format\n"
              << "  --verbose         Verbose output including metadata\n"
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
};

Config g_config;

std::vector<uint8_t> decompress(const uint8_t* data, size_t size) {
    if (!g_zstd_dctx) {
        g_zstd_dctx = ZSTD_createDCtx();
    }

    // Get decompressed size (stored in frame header)
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Use a reasonable default if size is unknown
        decompressed_size = size * 10;
    } else if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        LOG(ERROR) << "Invalid zstd frame";
        return {};
    }

    std::vector<uint8_t> decompressed(decompressed_size);
    size_t result = ZSTD_decompressDCtx(g_zstd_dctx,
                                         decompressed.data(), decompressed.size(),
                                         data, size);

    if (ZSTD_isError(result)) {
        LOG(ERROR) << "Decompression failed: " << ZSTD_getErrorName(result);
        return {};
    }

    decompressed.resize(result);
    return decompressed;
}

std::string quality_to_string(vep::transfer::Quality q) {
    switch (q) {
        case vep::transfer::QUALITY_VALID: return "VALID";
        case vep::transfer::QUALITY_INVALID: return "INVALID";
        case vep::transfer::QUALITY_NOT_AVAILABLE: return "NOT_AVAILABLE";
        default: return "UNKNOWN";
    }
}

std::string severity_to_string(vep::transfer::Severity s) {
    switch (s) {
        case vep::transfer::SEVERITY_INFO: return "INFO";
        case vep::transfer::SEVERITY_WARNING: return "WARNING";
        case vep::transfer::SEVERITY_ERROR: return "ERROR";
        case vep::transfer::SEVERITY_CRITICAL: return "CRITICAL";
        default: return "UNKNOWN";
    }
}

void process_signal_batch(const std::vector<uint8_t>& data) {
    vep::transfer::SignalBatch batch;
    if (!batch.ParseFromArray(data.data(), data.size())) {
        LOG(ERROR) << "Failed to parse SignalBatch";
        return;
    }

    // Get current time for delay calculation
    auto now = std::chrono::system_clock::now();
    uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();

    // Calculate min/max timestamps in batch
    uint64_t min_ts = UINT64_MAX;
    uint64_t max_ts = 0;
    for (const auto& sig : batch.signals()) {
        uint64_t ts = batch.base_timestamp_ms() + sig.timestamp_delta_ms();
        if (ts < min_ts) min_ts = ts;
        if (ts > max_ts) max_ts = ts;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "signal_batch";
        j["source_id"] = batch.source_id();
        j["sequence"] = batch.sequence();
        j["base_timestamp_ms"] = batch.base_timestamp_ms();
        j["received_at_ms"] = now_ms;
        j["first_signal_delay_ms"] = static_cast<int64_t>(now_ms - min_ts);
        j["last_signal_delay_ms"] = static_cast<int64_t>(now_ms - max_ts);
        j["signals"] = json::array();

        for (const auto& sig : batch.signals()) {
            json js;
            if (sig.has_path()) {
                js["path"] = sig.path();
            } else {
                js["path_id"] = sig.path_id();
            }
            js["timestamp_ms"] = batch.base_timestamp_ms() + sig.timestamp_delta_ms();
            js["quality"] = quality_to_string(sig.quality());
            js["value"] = signal_value_to_json(sig);

            j["signals"].push_back(js);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Signal Batch [" << batch.base_timestamp_ms() << " ms] ===\n";
        std::cout << "Source: " << batch.source_id()
                  << " | Seq: " << batch.sequence()
                  << " | Signals: " << batch.signals_size()
                  << " | Span: " << (max_ts - min_ts) << " ms\n";
        std::cout << "Delay: first=" << (now_ms - min_ts) << " ms"
                  << ", last=" << (now_ms - max_ts) << " ms\n";

        for (const auto& sig : batch.signals()) {
            std::string path = sig.has_path() ? sig.path() : ("path_id:" + std::to_string(sig.path_id()));
            uint64_t ts = batch.base_timestamp_ms() + sig.timestamp_delta_ms();

            std::cout << "  [" << ts << "] " << path << " = " << signal_value_to_string(sig);

            if (sig.quality() != vep::transfer::QUALITY_VALID) {
                std::cout << " [" << quality_to_string(sig.quality()) << "]";
            }
            std::cout << "\n";
        }
    }
}

void process_event_batch(const std::vector<uint8_t>& data) {
    vep::transfer::EventBatch batch;
    if (!batch.ParseFromArray(data.data(), data.size())) {
        LOG(ERROR) << "Failed to parse EventBatch";
        return;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "event_batch";
        j["source_id"] = batch.source_id();
        j["sequence"] = batch.sequence();
        j["events"] = json::array();

        for (const auto& evt : batch.events()) {
            json je;
            je["event_id"] = evt.event_id();
            je["timestamp_ms"] = batch.base_timestamp_ms() + evt.timestamp_delta_ms();
            je["category"] = evt.category();
            je["event_type"] = evt.event_type();
            je["severity"] = severity_to_string(evt.severity());
            if (!evt.payload().empty()) {
                je["payload_size"] = evt.payload().size();
            }
            j["events"].push_back(je);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Event Batch ===\n";
        if (g_config.verbose) {
            std::cout << "Source: " << batch.source_id()
                      << " | Seq: " << batch.sequence() << "\n";
        }

        for (const auto& evt : batch.events()) {
            std::cout << "  [" << severity_to_string(evt.severity()) << "] "
                      << evt.category() << "/" << evt.event_type()
                      << " (" << evt.event_id() << ")\n";
        }
    }
}

void process_metrics_batch(const std::vector<uint8_t>& data) {
    vep::transfer::MetricsBatch batch;
    if (!batch.ParseFromArray(data.data(), data.size())) {
        LOG(ERROR) << "Failed to parse MetricsBatch";
        return;
    }

    if (g_config.json_output) {
        json j;
        j["type"] = "metrics_batch";
        j["source_id"] = batch.source_id();
        j["sequence"] = batch.sequence();
        j["metrics"] = json::array();

        for (const auto& m : batch.metrics()) {
            json jm;
            jm["name"] = m.name();
            if (m.has_gauge()) {
                jm["type"] = "gauge";
                jm["value"] = m.gauge();
            } else if (m.has_counter()) {
                jm["type"] = "counter";
                jm["value"] = m.counter();
            } else if (m.has_histogram()) {
                jm["type"] = "histogram";
                jm["sample_count"] = m.histogram().sample_count();
                jm["sample_sum"] = m.histogram().sample_sum();
            }
            j["metrics"].push_back(jm);
        }

        std::cout << j.dump(2) << "\n";
    } else {
        std::cout << "\n=== Metrics Batch ===\n";
        if (g_config.verbose) {
            std::cout << "Source: " << batch.source_id()
                      << " | Seq: " << batch.sequence() << "\n";
        }

        for (const auto& m : batch.metrics()) {
            if (m.has_gauge()) {
                std::cout << "  [GAUGE] " << m.name() << " = " << m.gauge() << "\n";
            } else if (m.has_counter()) {
                std::cout << "  [COUNTER] " << m.name() << " = " << m.counter() << "\n";
            } else if (m.has_histogram()) {
                std::cout << "  [HISTOGRAM] " << m.name()
                          << " count=" << m.histogram().sample_count()
                          << " sum=" << m.histogram().sample_sum() << "\n";
            }
        }
    }
}

void on_message(struct mosquitto* /*mosq*/, void* /*obj*/,
                const struct mosquitto_message* msg) {
    std::string topic(msg->topic);

    LOG(INFO) << "Received message on " << topic
              << " (" << msg->payloadlen << " bytes compressed)";

    // Decompress
    auto decompressed = decompress(static_cast<uint8_t*>(msg->payload), msg->payloadlen);
    if (decompressed.empty()) {
        return;
    }

    LOG(INFO) << "Decompressed to " << decompressed.size() << " bytes ("
              << std::fixed << std::setprecision(1)
              << (100.0 * msg->payloadlen / decompressed.size()) << "% of original)";

    // Determine type from topic
    if (topic.find("/signals") != std::string::npos) {
        process_signal_batch(decompressed);
    } else if (topic.find("/events") != std::string::npos) {
        process_event_batch(decompressed);
    } else if (topic.find("/metrics") != std::string::npos) {
        process_metrics_batch(decompressed);
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

    LOG(INFO) << "Cloud Backend Simulator starting...";

    // Parse configuration
    g_config = parse_args(argc, argv);
    LOG(INFO) << "MQTT Broker: " << g_config.broker_host << ":" << g_config.broker_port;
    LOG(INFO) << "Subscribe topic: " << g_config.topic_subscribe;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Initialize Mosquitto
    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("cloud_backend_sim", true, nullptr);
    if (!mosq) {
        LOG(ERROR) << "Failed to create Mosquitto client";
        return 1;
    }

    // Set callbacks
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_disconnect_callback_set(mosq, on_disconnect);
    mosquitto_message_callback_set(mosq, on_message);

    // Connect
    int rc = mosquitto_connect(mosq, g_config.broker_host.c_str(), g_config.broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(ERROR) << "Connect failed: " << mosquitto_strerror(rc);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    LOG(INFO) << "Cloud Backend Simulator running. Press Ctrl+C to stop.";

    // Event loop
    while (g_running) {
        rc = mosquitto_loop(mosq, 100, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG(WARNING) << "Loop error: " << mosquitto_strerror(rc);
            // Try to reconnect
            mosquitto_reconnect(mosq);
        }
    }

    // Cleanup
    LOG(INFO) << "Shutting down...";
    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (g_zstd_dctx) {
        ZSTD_freeDCtx(g_zstd_dctx);
    }

    LOG(INFO) << "Cloud Backend Simulator stopped.";
    return 0;
}
