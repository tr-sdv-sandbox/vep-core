// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief VEP MQTT Logger - Receives and decodes compressed MQTT messages
///
/// Subscribes to MQTT topics, decompresses with zstd, decodes TransferBatch
/// protobuf, and logs data. This is the public/open-source version that
/// does not use CCPMainMessage envelope.
///
/// Message format:
///   MQTT payload -> zstd compressed -> TransferBatch (interleaved items)
///
/// This tool serves as a template for cloud-side ingestion (e.g., MQTT-to-Kafka).
///
/// Usage:
///   vep_mqtt_logger --broker localhost --port 1883 --topic "telemetry/#"
///   vep_mqtt_logger --json  # Output JSON format

#include "transfer.pb.h"

#include <glog/logging.h>
#include <zstd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <map>
#include <mosquitto.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

// Configuration
struct Config {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string topic_pattern = "telemetry/#";
    bool json_output = false;
    bool verbose = false;
};

Config g_config;
std::atomic<bool> g_running{true};
ZSTD_DCtx* g_zstd_ctx = nullptr;

// Statistics per source
struct SourceStats {
    uint64_t messages_received = 0;
    uint64_t bytes_compressed = 0;
    uint64_t bytes_decompressed = 0;
    uint64_t signals_count = 0;
    uint64_t events_count = 0;
    uint64_t metrics_count = 0;
    uint64_t logs_count = 0;
    std::chrono::steady_clock::time_point first_seen;
    std::chrono::steady_clock::time_point last_seen;
};

std::map<std::string, SourceStats> g_source_stats;

void signal_handler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

std::vector<uint8_t> decompress(const uint8_t* data, size_t size) {
    if (!g_zstd_ctx) {
        g_zstd_ctx = ZSTD_createDCtx();
    }

    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data, size);
    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        decompressed_size = size * 10;
    } else if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        LOG(ERROR) << "Invalid zstd frame";
        return {};
    }

    std::vector<uint8_t> decompressed(decompressed_size);
    size_t result = ZSTD_decompressDCtx(g_zstd_ctx,
                                         decompressed.data(), decompressed.size(),
                                         data, size);

    if (ZSTD_isError(result)) {
        LOG(ERROR) << "Decompression failed: " << ZSTD_getErrorName(result);
        return {};
    }

    decompressed.resize(result);
    return decompressed;
}

// Quality/Severity to string
const char* quality_str(vep::transfer::Quality q) {
    switch (q) {
        case vep::transfer::QUALITY_VALID: return "VALID";
        case vep::transfer::QUALITY_INVALID: return "INVALID";
        case vep::transfer::QUALITY_NOT_AVAILABLE: return "N/A";
        default: return "?";
    }
}

const char* severity_str(vep::transfer::Severity s) {
    switch (s) {
        case vep::transfer::SEVERITY_INFO: return "INFO";
        case vep::transfer::SEVERITY_WARNING: return "WARN";
        case vep::transfer::SEVERITY_ERROR: return "ERROR";
        case vep::transfer::SEVERITY_CRITICAL: return "CRIT";
        default: return "?";
    }
}

const char* log_level_str(vep::transfer::LogLevel l) {
    switch (l) {
        case vep::transfer::LOG_LEVEL_DEBUG: return "DEBUG";
        case vep::transfer::LOG_LEVEL_INFO: return "INFO";
        case vep::transfer::LOG_LEVEL_WARN: return "WARN";
        case vep::transfer::LOG_LEVEL_ERROR: return "ERROR";
        default: return "?";
    }
}

// Get signal value as string
std::string signal_value_str(const vep::transfer::Signal& sig) {
    std::ostringstream oss;
    switch (sig.value_case()) {
        case vep::transfer::Signal::kBoolVal:
            oss << (sig.bool_val() ? "true" : "false");
            break;
        case vep::transfer::Signal::kInt32Val:
            oss << sig.int32_val();
            break;
        case vep::transfer::Signal::kInt64Val:
            oss << sig.int64_val();
            break;
        case vep::transfer::Signal::kUint32Val:
            oss << sig.uint32_val();
            break;
        case vep::transfer::Signal::kUint64Val:
            oss << sig.uint64_val();
            break;
        case vep::transfer::Signal::kFloatVal:
            oss << std::fixed << std::setprecision(3) << sig.float_val();
            break;
        case vep::transfer::Signal::kDoubleVal:
            oss << std::fixed << std::setprecision(6) << sig.double_val();
            break;
        case vep::transfer::Signal::kStringVal:
            oss << "\"" << sig.string_val() << "\"";
            break;
        default:
            oss << "(complex)";
    }
    return oss.str();
}

// Process TransferBatch (unified format with interleaved items)
void process_transfer_batch(const std::string& topic, const std::vector<uint8_t>& data) {
    vep::transfer::TransferBatch batch;
    if (!batch.ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        LOG(ERROR) << "Failed to parse TransferBatch from " << topic;
        return;
    }

    const std::string& source_id = batch.source_id();
    auto& stats = g_source_stats[source_id];

    // Count items by type
    int signal_count = 0, event_count = 0, metric_count = 0, log_count = 0;
    for (const auto& item : batch.items()) {
        switch (item.item_case()) {
            case vep::transfer::TransferItem::kSignal: signal_count++; break;
            case vep::transfer::TransferItem::kEvent: event_count++; break;
            case vep::transfer::TransferItem::kMetric: metric_count++; break;
            case vep::transfer::TransferItem::kLog: log_count++; break;
            default: break;
        }
    }

    stats.signals_count += signal_count;
    stats.events_count += event_count;
    stats.metrics_count += metric_count;
    stats.logs_count += log_count;

    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    if (g_config.json_output) {
        std::cout << "{\"type\":\"transfer_batch\",\"topic\":\"" << topic
                  << "\",\"source\":\"" << source_id
                  << "\",\"seq\":" << batch.sequence()
                  << ",\"items\":" << batch.items_size()
                  << ",\"signals\":" << signal_count
                  << ",\"events\":" << event_count
                  << ",\"metrics\":" << metric_count
                  << ",\"logs\":" << log_count
                  << "}\n";
    } else {
        std::cout << "\n[" << topic << "] TRANSFER_BATCH (" << batch.items_size() << " items)"
                  << " src=" << source_id << " seq=" << batch.sequence()
                  << " [S:" << signal_count << " E:" << event_count
                  << " M:" << metric_count << " L:" << log_count << "]\n";

        for (const auto& item : batch.items()) {
            uint64_t ts = batch.base_timestamp_ms() + item.timestamp_delta_ms();
            int64_t delay_ms = now_ms - static_cast<int64_t>(ts);

            switch (item.item_case()) {
                case vep::transfer::TransferItem::kSignal: {
                    const auto& sig = item.signal();
                    std::string path = sig.has_path() ? sig.path() : ("id:" + std::to_string(sig.path_id()));
                    std::cout << "  [SIG] " << path << " = " << signal_value_str(sig);
                    if (sig.quality() != vep::transfer::QUALITY_VALID) {
                        std::cout << " [" << quality_str(sig.quality()) << "]";
                    }
                    if (g_config.verbose) {
                        std::cout << " (delay=" << delay_ms << "ms)";
                    }
                    std::cout << "\n";
                    break;
                }
                case vep::transfer::TransferItem::kEvent: {
                    const auto& evt = item.event();
                    std::cout << "  [EVT] [" << severity_str(evt.severity()) << "] "
                              << evt.category() << "/" << evt.event_type()
                              << " (id=" << evt.event_id() << ")\n";
                    break;
                }
                case vep::transfer::TransferItem::kMetric: {
                    const auto& m = item.metric();
                    std::cout << "  [MET] ";
                    if (m.has_gauge()) {
                        std::cout << "[GAUGE] " << m.name() << " = " << m.gauge();
                    } else if (m.has_counter()) {
                        std::cout << "[COUNTER] " << m.name() << " = " << m.counter();
                    } else if (m.has_histogram()) {
                        std::cout << "[HISTO] " << m.name()
                                  << " count=" << m.histogram().sample_count()
                                  << " sum=" << m.histogram().sample_sum();
                    }
                    if (m.label_keys_size() > 0) {
                        std::cout << " {";
                        for (int i = 0; i < m.label_keys_size() && i < m.label_values_size(); ++i) {
                            if (i > 0) std::cout << ",";
                            std::cout << m.label_keys(i) << "=" << m.label_values(i);
                        }
                        std::cout << "}";
                    }
                    std::cout << "\n";
                    break;
                }
                case vep::transfer::TransferItem::kLog: {
                    const auto& log = item.log();
                    std::cout << "  [LOG] [" << log_level_str(log.level()) << "] "
                              << log.component() << ": " << log.message() << "\n";
                    break;
                }
                default:
                    std::cout << "  [???] Unknown item type\n";
            }
        }
    }
}

// MQTT message callback
void on_message(struct mosquitto* /*mosq*/, void* /*obj*/,
                const struct mosquitto_message* msg) {
    std::string topic(msg->topic);

    // Decompress payload directly (no CCPMainMessage envelope)
    auto decompressed = decompress(static_cast<uint8_t*>(msg->payload), msg->payloadlen);
    if (decompressed.empty() && msg->payloadlen > 0) {
        return;
    }

    // Extract source_id from first parse for stats
    vep::transfer::TransferBatch batch;
    if (batch.ParseFromArray(decompressed.data(), static_cast<int>(decompressed.size()))) {
        auto& stats = g_source_stats[batch.source_id()];
        auto now = std::chrono::steady_clock::now();
        if (stats.messages_received == 0) {
            stats.first_seen = now;
        }
        stats.last_seen = now;
        stats.messages_received++;
        stats.bytes_compressed += msg->payloadlen;
        stats.bytes_decompressed += decompressed.size();
    }

    double ratio = 100.0 * msg->payloadlen / std::max(decompressed.size(), size_t(1));

    if (g_config.verbose) {
        LOG(INFO) << topic
                  << " (" << msg->payloadlen << " -> " << decompressed.size()
                  << " bytes, " << std::fixed << std::setprecision(1) << ratio << "% zstd)";
    }

    // Process as TransferBatch (unified format)
    process_transfer_batch(topic, decompressed);
}

void on_connect(struct mosquitto* mosq, void* /*obj*/, int rc) {
    if (rc == 0) {
        LOG(INFO) << "Connected to MQTT broker";
        mosquitto_subscribe(mosq, nullptr, g_config.topic_pattern.c_str(), 0);
        LOG(INFO) << "Subscribed to: " << g_config.topic_pattern;
    } else {
        LOG(ERROR) << "Connection failed: " << mosquitto_connack_string(rc);
    }
}

void print_stats() {
    std::cout << "\n=== Statistics ===\n";
    for (const auto& [source_id, stats] : g_source_stats) {
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
            stats.last_seen - stats.first_seen).count();

        std::cout << "\nSource: " << source_id << "\n"
                  << "  Messages: " << stats.messages_received
                  << " over " << duration << "s\n"
                  << "  Data: " << stats.bytes_compressed << " compressed -> "
                  << stats.bytes_decompressed << " decompressed ("
                  << std::fixed << std::setprecision(1)
                  << (100.0 * stats.bytes_compressed / std::max(stats.bytes_decompressed, 1UL))
                  << "%)\n"
                  << "  Signals: " << stats.signals_count
                  << " | Events: " << stats.events_count
                  << " | Metrics: " << stats.metrics_count
                  << " | Logs: " << stats.logs_count << "\n";
    }
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "VEP MQTT Logger - Receives and decodes vehicle telemetry\n"
              << "\n"
              << "Message format:\n"
              << "  zstd compressed -> TransferBatch (interleaved items)\n"
              << "\n"
              << "Options:\n"
              << "  --broker HOST     MQTT broker host (default: localhost)\n"
              << "  --port PORT       MQTT broker port (default: 1883)\n"
              << "  --topic PATTERN   Topic pattern (default: telemetry/#)\n"
              << "  --json            Output JSON format\n"
              << "  --verbose         Verbose output with delay info\n"
              << "  --help            Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  " << prog << " --broker localhost\n"
              << "  " << prog << " --topic \"v1/telemetry/#\" --json\n"
              << "\n";
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
            config.topic_pattern = argv[++i];
        } else if (arg == "--json") {
            config.json_output = true;
        } else if (arg == "--verbose" || arg == "-v") {
            config.verbose = true;
        } else {
            LOG(WARNING) << "Unknown argument: " << arg;
        }
    }

    return config;
}

}  // namespace

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    std::cout << "VEP MQTT Logger\n";
    std::cout << "===============\n\n";

    g_config = parse_args(argc, argv);

    std::cout << "MQTT Broker: " << g_config.broker_host << ":" << g_config.broker_port << "\n";
    std::cout << "Topic:       " << g_config.topic_pattern << "\n\n";

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("vep_mqtt_logger", true, nullptr);
    if (!mosq) {
        LOG(ERROR) << "Failed to create Mosquitto client";
        return 1;
    }

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    LOG(INFO) << "Connecting to broker...";
    while (g_running) {
        int rc = mosquitto_connect(mosq, g_config.broker_host.c_str(), g_config.broker_port, 60);
        if (rc == MOSQ_ERR_SUCCESS) break;
        LOG(INFO) << "Waiting for broker...";
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }

    if (!g_running) {
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 0;
    }

    std::cout << "Listening for telemetry... Press Ctrl+C to stop.\n";
    std::cout << "=============================================\n";

    while (g_running) {
        int rc = mosquitto_loop(mosq, 100, 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            mosquitto_reconnect(mosq);
        }
    }

    print_stats();

    mosquitto_disconnect(mosq);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    if (g_zstd_ctx) {
        ZSTD_freeDCtx(g_zstd_ctx);
    }

    LOG(INFO) << "Stopped.";
    return 0;
}
