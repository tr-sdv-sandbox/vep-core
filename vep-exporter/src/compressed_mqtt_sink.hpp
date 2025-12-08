// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file compressed_mqtt_sink.hpp
/// @brief OutputSink implementation with protobuf encoding + zstd compression + MQTT transport
///
/// This sink:
/// 1. Batches incoming signals/events/metrics
/// 2. Encodes to protobuf (transfer.proto)
/// 3. Compresses with zstd
/// 4. Publishes to MQTT broker
///
/// Optimized for bandwidth-constrained vehicle-to-cloud links.

#include "output_sink.hpp"
#include "transfer.pb.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

// Forward declarations
struct mosquitto;
struct ZSTD_CCtx_s;
typedef struct ZSTD_CCtx_s ZSTD_CCtx;

namespace integration {

/// Configuration for compressed MQTT sink
struct CompressedMqttConfig {
    // MQTT settings
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string client_id = "vdr_exporter";
    std::string username;
    std::string password;
    int keepalive_sec = 60;
    int qos = 1;
    std::string topic_prefix = "v1/telemetry";

    // Batching settings
    size_t batch_max_signals = 100;       // Max signals per batch
    size_t batch_max_events = 50;         // Max events per batch
    size_t batch_max_metrics = 100;       // Max metrics per batch
    std::chrono::milliseconds batch_timeout{1000};  // Flush after this time (1s default for better compression)

    // Compression settings
    int zstd_compression_level = 3;       // 1-19, 3 is good balance
    bool use_dictionary = false;          // Use trained dictionary (future)
};

/// Compressed MQTT sink implementing OutputSink interface
class CompressedMqttSink : public OutputSink {
public:
    explicit CompressedMqttSink(const CompressedMqttConfig& config = {});
    ~CompressedMqttSink() override;

    CompressedMqttSink(const CompressedMqttSink&) = delete;
    CompressedMqttSink& operator=(const CompressedMqttSink&) = delete;

    // OutputSink interface
    bool start() override;
    void stop() override;
    void flush() override;

    void send(const vep_VssSignal& msg) override;
    void send(const vep_Event& msg) override;
    void send(const vep_OtelGauge& msg) override;
    void send(const vep_OtelCounter& msg) override;
    void send(const vep_OtelHistogram& msg) override;
    void send(const vep_OtelLogEntry& msg) override;
    void send(const vep_ScalarMeasurement& msg) override;
    void send(const vep_VectorMeasurement& msg) override;

    bool healthy() const override;
    SinkStats stats() const override;
    std::string name() const override { return "CompressedMqttSink"; }

    // Additional stats
    struct CompressionStats {
        uint64_t bytes_before_compression = 0;
        uint64_t bytes_after_compression = 0;
        uint64_t batches_sent = 0;
        double compression_ratio() const {
            return bytes_before_compression > 0
                ? static_cast<double>(bytes_after_compression) / bytes_before_compression
                : 0.0;
        }
    };
    CompressionStats compression_stats() const;

private:
    // Internal signal storage for batching
    // Stores pre-converted protobuf signal to avoid dangling DDS pointers
    struct PendingSignal {
        std::string path;
        int64_t timestamp_ms;
        vep::transfer::Quality quality;
        vep::transfer::Signal proto_signal;  // Pre-converted protobuf
    };

    struct PendingEvent {
        std::string event_id;
        int64_t timestamp_ms;
        std::string category;
        std::string event_type;
        int severity;
        std::vector<uint8_t> payload;
    };

    struct PendingMetric {
        std::string name;
        int64_t timestamp_ms;
        int metric_type;  // 0=gauge, 1=counter, 2=histogram
        double value;
        std::vector<std::string> label_keys;
        std::vector<std::string> label_values;
    };

    void batch_loop();
    void flush_signals();
    void flush_events();
    void flush_metrics();

    std::vector<uint8_t> compress(const std::string& data);
    void mqtt_publish(const std::string& topic, const std::vector<uint8_t>& payload);

    // Mosquitto callbacks
    static void on_connect(struct mosquitto* mosq, void* obj, int rc);
    static void on_disconnect(struct mosquitto* mosq, void* obj, int rc);

    CompressedMqttConfig config_;
    std::string source_id_ = "vdr";

    // MQTT
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> connected_{false};

    // Zstd
    ZSTD_CCtx* zstd_ctx_ = nullptr;

    // State
    std::atomic<bool> running_{false};

    // Batching queues
    std::mutex signals_mutex_;
    std::vector<PendingSignal> pending_signals_;
    int64_t signals_base_timestamp_ = 0;

    std::mutex events_mutex_;
    std::vector<PendingEvent> pending_events_;

    std::mutex metrics_mutex_;
    std::vector<PendingMetric> pending_metrics_;

    // Background thread for batching/flushing
    std::thread batch_thread_;
    std::condition_variable batch_cv_;
    std::mutex batch_cv_mutex_;

    // Sequence numbers
    std::atomic<uint32_t> signal_seq_{0};
    std::atomic<uint32_t> event_seq_{0};
    std::atomic<uint32_t> metric_seq_{0};

    // Stats
    mutable std::mutex stats_mutex_;
    SinkStats stats_;
    CompressionStats compression_stats_;
};

}  // namespace integration
