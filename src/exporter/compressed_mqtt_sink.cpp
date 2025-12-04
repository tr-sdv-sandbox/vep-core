// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "exporter/compressed_mqtt_sink.hpp"
#include "exporter/dds_proto_conversion.hpp"
#include "transfer.pb.h"

#include <glog/logging.h>
#include <mosquitto.h>
#include <zstd.h>

#include <chrono>

namespace integration {

CompressedMqttSink::CompressedMqttSink(const CompressedMqttConfig& config)
    : config_(config) {
}

CompressedMqttSink::~CompressedMqttSink() {
    stop();
}

bool CompressedMqttSink::start() {
    if (running_) {
        return true;
    }

    // Initialize zstd context
    zstd_ctx_ = ZSTD_createCCtx();
    if (!zstd_ctx_) {
        LOG(ERROR) << "Failed to create zstd compression context";
        return false;
    }
    ZSTD_CCtx_setParameter(zstd_ctx_, ZSTD_c_compressionLevel, config_.zstd_compression_level);

    // Initialize mosquitto
    mosquitto_lib_init();

    mosq_ = mosquitto_new(config_.client_id.c_str(), true, this);
    if (!mosq_) {
        LOG(ERROR) << "Failed to create mosquitto client";
        ZSTD_freeCCtx(zstd_ctx_);
        zstd_ctx_ = nullptr;
        return false;
    }

    mosquitto_connect_callback_set(mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);

    if (!config_.username.empty()) {
        mosquitto_username_pw_set(mosq_, config_.username.c_str(),
                                  config_.password.c_str());
    }

    int rc = mosquitto_connect_async(mosq_, config_.broker_host.c_str(),
                                      config_.broker_port, config_.keepalive_sec);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(ERROR) << "MQTT connect failed: " << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        ZSTD_freeCCtx(zstd_ctx_);
        zstd_ctx_ = nullptr;
        return false;
    }

    mosquitto_loop_start(mosq_);

    running_ = true;
    batch_thread_ = std::thread(&CompressedMqttSink::batch_loop, this);

    LOG(INFO) << "CompressedMqttSink started, connecting to "
              << config_.broker_host << ":" << config_.broker_port;
    return true;
}

void CompressedMqttSink::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    batch_cv_.notify_all();

    if (batch_thread_.joinable()) {
        batch_thread_.join();
    }

    // Final flush
    flush();

    if (mosq_) {
        mosquitto_loop_stop(mosq_, false);
        mosquitto_disconnect(mosq_);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();

    if (zstd_ctx_) {
        ZSTD_freeCCtx(zstd_ctx_);
        zstd_ctx_ = nullptr;
    }

    connected_ = false;

    LOG(INFO) << "CompressedMqttSink stopped. Stats: sent=" << stats_.messages_sent
              << " failed=" << stats_.messages_failed
              << " compression_ratio=" << compression_stats_.compression_ratio();
}

void CompressedMqttSink::flush() {
    flush_signals();
    flush_events();
    flush_metrics();
}

void CompressedMqttSink::batch_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(batch_cv_mutex_);
        batch_cv_.wait_for(lock, config_.batch_timeout);

        if (!running_) break;

        // Flush all pending data (handles both size threshold and timeout)
        lock.unlock();
        flush();
        lock.lock();
    }
}

void CompressedMqttSink::send(const vss_Signal& msg) {
    if (!running_) return;

    PendingSignal sig;
    sig.path = msg.path ? msg.path : "";
    sig.timestamp_ms = msg.header.timestamp_ns / 1000000;
    sig.quality = convert_quality(msg.quality);

    // Convert value to protobuf immediately to avoid dangling DDS pointers
    // (DDS uses char* and sequence buffers that become invalid after message is freed)
    convert_value_to_signal(msg.value, &sig.proto_signal);

    {
        std::lock_guard<std::mutex> lock(signals_mutex_);
        if (pending_signals_.empty()) {
            signals_base_timestamp_ = sig.timestamp_ms;
        }
        pending_signals_.push_back(std::move(sig));

        if (pending_signals_.size() >= config_.batch_max_signals) {
            batch_cv_.notify_one();
        }
    }
}

void CompressedMqttSink::send(const telemetry_events_Event& msg) {
    if (!running_) return;

    PendingEvent evt;
    evt.event_id = msg.event_id ? msg.event_id : "";
    evt.timestamp_ms = msg.header.timestamp_ns / 1000000;
    evt.category = msg.category ? msg.category : "";
    evt.event_type = msg.event_type ? msg.event_type : "";
    evt.severity = static_cast<int>(msg.severity);

    // Note: Event no longer has payload field - it has attributes and context instead
    // Leave payload empty for now

    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        pending_events_.push_back(std::move(evt));

        if (pending_events_.size() >= config_.batch_max_events) {
            batch_cv_.notify_one();
        }
    }
}

void CompressedMqttSink::send(const telemetry_metrics_Gauge& msg) {
    if (!running_) return;

    PendingMetric met;
    met.name = msg.name ? msg.name : "";
    met.timestamp_ms = msg.header.timestamp_ns / 1000000;
    met.metric_type = 0;  // gauge
    met.value = msg.value;

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            met.label_keys.push_back(msg.labels._buffer[i].key);
            met.label_values.push_back(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        pending_metrics_.push_back(std::move(met));
    }
}

void CompressedMqttSink::send(const telemetry_metrics_Counter& msg) {
    if (!running_) return;

    PendingMetric met;
    met.name = msg.name ? msg.name : "";
    met.timestamp_ms = msg.header.timestamp_ns / 1000000;
    met.metric_type = 1;  // counter
    met.value = msg.value;

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            met.label_keys.push_back(msg.labels._buffer[i].key);
            met.label_values.push_back(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        pending_metrics_.push_back(std::move(met));
    }
}

void CompressedMqttSink::send(const telemetry_metrics_Histogram& msg) {
    // TODO: Implement histogram batching
    (void)msg;
}

void CompressedMqttSink::send(const telemetry_logs_LogEntry& msg) {
    // TODO: Implement log batching
    (void)msg;
}

void CompressedMqttSink::send(const telemetry_diagnostics_ScalarMeasurement& msg) {
    // Map to gauge metric
    telemetry_metrics_Gauge gauge = {};
    gauge.header = msg.header;
    gauge.name = msg.variable_id;
    gauge.value = msg.value;
    gauge.labels._length = 0;
    gauge.labels._buffer = nullptr;
    send(gauge);
}

void CompressedMqttSink::send(const telemetry_diagnostics_VectorMeasurement& msg) {
    // TODO: Implement vector measurement batching
    (void)msg;
}

void CompressedMqttSink::flush_signals() {
    std::vector<PendingSignal> signals;
    int64_t base_ts;

    {
        std::lock_guard<std::mutex> lock(signals_mutex_);
        if (pending_signals_.empty()) return;
        signals = std::move(pending_signals_);
        pending_signals_.clear();
        base_ts = signals_base_timestamp_;
    }

    // Build protobuf batch
    vdr::transfer::SignalBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(signal_seq_++);

    for (const auto& sig : signals) {
        auto* pb_sig = batch.add_signals();
        pb_sig->set_path(sig.path);
        pb_sig->set_timestamp_delta_ms(
            static_cast<uint32_t>(sig.timestamp_ms - base_ts));
        pb_sig->set_quality(sig.quality);

        // Copy the pre-converted value from proto_signal
        pb_sig->MergeFrom(sig.proto_signal);
    }

    std::string serialized;
    batch.SerializeToString(&serialized);

    auto compressed = compress(serialized);
    mqtt_publish("signals", compressed);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_sent += signals.size();
        compression_stats_.bytes_before_compression += serialized.size();
        compression_stats_.bytes_after_compression += compressed.size();
        compression_stats_.batches_sent++;
    }
}

void CompressedMqttSink::flush_events() {
    std::vector<PendingEvent> events;

    {
        std::lock_guard<std::mutex> lock(events_mutex_);
        if (pending_events_.empty()) return;
        events = std::move(pending_events_);
        pending_events_.clear();
    }

    int64_t base_ts = events.empty() ? 0 : events[0].timestamp_ms;

    vdr::transfer::EventBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(event_seq_++);

    for (const auto& evt : events) {
        auto* pb_evt = batch.add_events();
        pb_evt->set_event_id(evt.event_id);
        pb_evt->set_timestamp_delta_ms(
            static_cast<uint32_t>(evt.timestamp_ms - base_ts));
        pb_evt->set_category(evt.category);
        pb_evt->set_event_type(evt.event_type);
        pb_evt->set_severity(static_cast<vdr::transfer::Severity>(evt.severity));
        if (!evt.payload.empty()) {
            pb_evt->set_payload(evt.payload.data(), evt.payload.size());
        }
    }

    std::string serialized;
    batch.SerializeToString(&serialized);

    auto compressed = compress(serialized);
    mqtt_publish("events", compressed);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_sent += events.size();
        compression_stats_.bytes_before_compression += serialized.size();
        compression_stats_.bytes_after_compression += compressed.size();
        compression_stats_.batches_sent++;
    }
}

void CompressedMqttSink::flush_metrics() {
    std::vector<PendingMetric> metrics;

    {
        std::lock_guard<std::mutex> lock(metrics_mutex_);
        if (pending_metrics_.empty()) return;
        metrics = std::move(pending_metrics_);
        pending_metrics_.clear();
    }

    int64_t base_ts = metrics.empty() ? 0 : metrics[0].timestamp_ms;

    vdr::transfer::MetricsBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(metric_seq_++);

    for (const auto& met : metrics) {
        auto* pb_met = batch.add_metrics();
        pb_met->set_name(met.name);
        pb_met->set_timestamp_delta_ms(
            static_cast<uint32_t>(met.timestamp_ms - base_ts));

        if (met.metric_type == 0) {
            pb_met->set_gauge(met.value);
        } else if (met.metric_type == 1) {
            pb_met->set_counter(met.value);
        }

        for (const auto& key : met.label_keys) {
            pb_met->add_label_keys(key);
        }
        for (const auto& val : met.label_values) {
            pb_met->add_label_values(val);
        }
    }

    std::string serialized;
    batch.SerializeToString(&serialized);

    auto compressed = compress(serialized);
    mqtt_publish("metrics", compressed);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_sent += metrics.size();
        compression_stats_.bytes_before_compression += serialized.size();
        compression_stats_.bytes_after_compression += compressed.size();
        compression_stats_.batches_sent++;
    }
}

std::vector<uint8_t> CompressedMqttSink::compress(const std::string& data) {
    size_t max_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed(max_size);

    size_t result = ZSTD_compressCCtx(
        zstd_ctx_,
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        config_.zstd_compression_level);

    if (ZSTD_isError(result)) {
        LOG(WARNING) << "Compression failed: " << ZSTD_getErrorName(result);
        // Fall back to uncompressed
        compressed.assign(data.begin(), data.end());
    } else {
        compressed.resize(result);
    }

    return compressed;
}

void CompressedMqttSink::mqtt_publish(const std::string& topic,
                                       const std::vector<uint8_t>& payload) {
    if (!connected_ || !mosq_) {
        LOG(WARNING) << "MQTT not connected, dropping message for topic: " << topic;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_failed++;
        return;
    }

    std::string full_topic = config_.topic_prefix + "/" + topic;

    int rc = mosquitto_publish(mosq_, nullptr,
                               full_topic.c_str(),
                               static_cast<int>(payload.size()),
                               payload.data(),
                               config_.qos,
                               false);

    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_EVERY_N(WARNING, 100) << "MQTT publish failed: " << mosquitto_strerror(rc);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_failed++;
    } else {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_sent += payload.size();
        stats_.last_send_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }
}

bool CompressedMqttSink::healthy() const {
    return running_ && connected_;
}

SinkStats CompressedMqttSink::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

CompressedMqttSink::CompressionStats CompressedMqttSink::compression_stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return compression_stats_;
}

void CompressedMqttSink::on_connect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<CompressedMqttSink*>(obj);
    if (rc == 0) {
        self->connected_ = true;
        LOG(INFO) << "CompressedMqttSink connected to broker";
    } else {
        self->connected_ = false;
        LOG(WARNING) << "MQTT connection failed: " << mosquitto_connack_string(rc);
    }
}

void CompressedMqttSink::on_disconnect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<CompressedMqttSink*>(obj);
    self->connected_ = false;
    if (rc != 0) {
        LOG(WARNING) << "MQTT unexpected disconnect: " << mosquitto_strerror(rc);
    }
}

}  // namespace integration
