// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "batch_builder.hpp"

namespace vep::exporter {

// ============================================================================
// SignalBatchBuilder
// ============================================================================

SignalBatchBuilder::SignalBatchBuilder(const std::string& source_id)
    : source_id_(source_id) {
}

void SignalBatchBuilder::add(const vep_VssSignal& msg) {
    PendingSignal sig;
    sig.path = msg.path ? msg.path : "";
    sig.timestamp_ms = msg.header.timestamp_ns / 1000000;
    sig.quality = convert_quality(msg.quality);

    // Convert value immediately to avoid dangling DDS pointers
    convert_value_to_signal(msg.value, &sig.proto_signal);

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = sig.timestamp_ms;
    }
    pending_.push_back(std::move(sig));
}

bool SignalBatchBuilder::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

size_t SignalBatchBuilder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::vector<uint8_t> SignalBatchBuilder::build() {
    std::vector<PendingSignal> signals;
    int64_t base_ts;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) {
            return {};
        }
        signals = std::move(pending_);
        pending_.clear();
        base_ts = base_timestamp_ms_;
    }

    vep::transfer::SignalBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(sequence_++);

    for (const auto& sig : signals) {
        auto* pb_sig = batch.add_signals();
        pb_sig->set_path(sig.path);
        pb_sig->set_timestamp_delta_ms(
            static_cast<uint32_t>(sig.timestamp_ms - base_ts));
        pb_sig->set_quality(sig.quality);
        pb_sig->MergeFrom(sig.proto_signal);
    }

    std::string serialized;
    batch.SerializeToString(&serialized);
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

void SignalBatchBuilder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    base_timestamp_ms_ = 0;
}

// ============================================================================
// EventBatchBuilder
// ============================================================================

EventBatchBuilder::EventBatchBuilder(const std::string& source_id)
    : source_id_(source_id) {
}

void EventBatchBuilder::add(const vep_Event& msg) {
    PendingEvent evt;
    evt.event_id = msg.event_id ? msg.event_id : "";
    evt.timestamp_ms = msg.header.timestamp_ns / 1000000;
    evt.category = msg.category ? msg.category : "";
    evt.event_type = msg.event_type ? msg.event_type : "";
    evt.severity = static_cast<int>(msg.severity);

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(evt));
}

bool EventBatchBuilder::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

size_t EventBatchBuilder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::vector<uint8_t> EventBatchBuilder::build() {
    std::vector<PendingEvent> events;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) {
            return {};
        }
        events = std::move(pending_);
        pending_.clear();
    }

    int64_t base_ts = events[0].timestamp_ms;

    vep::transfer::EventBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(sequence_++);

    for (const auto& evt : events) {
        auto* pb_evt = batch.add_events();
        pb_evt->set_event_id(evt.event_id);
        pb_evt->set_timestamp_delta_ms(
            static_cast<uint32_t>(evt.timestamp_ms - base_ts));
        pb_evt->set_category(evt.category);
        pb_evt->set_event_type(evt.event_type);
        pb_evt->set_severity(static_cast<vep::transfer::Severity>(evt.severity));
    }

    std::string serialized;
    batch.SerializeToString(&serialized);
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

void EventBatchBuilder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

// ============================================================================
// MetricsBatchBuilder
// ============================================================================

MetricsBatchBuilder::MetricsBatchBuilder(const std::string& source_id)
    : source_id_(source_id) {
}

void MetricsBatchBuilder::add(const vep_OtelGauge& msg) {
    PendingMetric met;
    met.name = msg.name ? msg.name : "";
    met.timestamp_ms = msg.header.timestamp_ns / 1000000;
    met.metric_type = 0;  // gauge
    met.value = msg.value;

    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        met.label_keys.push_back("service");
        met.label_values.push_back(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            met.label_keys.push_back(msg.labels._buffer[i].key);
            met.label_values.push_back(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(met));
}

void MetricsBatchBuilder::add(const vep_OtelCounter& msg) {
    PendingMetric met;
    met.name = msg.name ? msg.name : "";
    met.timestamp_ms = msg.header.timestamp_ns / 1000000;
    met.metric_type = 1;  // counter
    met.value = msg.value;

    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        met.label_keys.push_back("service");
        met.label_values.push_back(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            met.label_keys.push_back(msg.labels._buffer[i].key);
            met.label_values.push_back(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(met));
}

void MetricsBatchBuilder::add(const vep_OtelHistogram& msg) {
    PendingMetric met;
    met.name = msg.name ? msg.name : "";
    met.timestamp_ms = msg.header.timestamp_ns / 1000000;
    met.metric_type = 2;  // histogram
    met.sample_count = msg.sample_count;
    met.sample_sum = msg.sample_sum;

    for (uint32_t i = 0; i < msg.buckets._length; ++i) {
        met.bucket_bounds.push_back(msg.buckets._buffer[i].upper_bound);
        met.bucket_counts.push_back(msg.buckets._buffer[i].cumulative_count);
    }

    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        met.label_keys.push_back("service");
        met.label_values.push_back(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            met.label_keys.push_back(msg.labels._buffer[i].key);
            met.label_values.push_back(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(met));
}

bool MetricsBatchBuilder::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

size_t MetricsBatchBuilder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::vector<uint8_t> MetricsBatchBuilder::build() {
    std::vector<PendingMetric> metrics;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) {
            return {};
        }
        metrics = std::move(pending_);
        pending_.clear();
    }

    int64_t base_ts = metrics[0].timestamp_ms;

    vep::transfer::MetricsBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(sequence_++);

    for (const auto& met : metrics) {
        auto* pb_met = batch.add_metrics();
        pb_met->set_name(met.name);
        pb_met->set_timestamp_delta_ms(
            static_cast<uint32_t>(met.timestamp_ms - base_ts));

        if (met.metric_type == 0) {
            pb_met->set_gauge(met.value);
        } else if (met.metric_type == 1) {
            pb_met->set_counter(met.value);
        } else if (met.metric_type == 2) {
            auto* hist = pb_met->mutable_histogram();
            hist->set_sample_count(met.sample_count);
            hist->set_sample_sum(met.sample_sum);
            for (const auto& bound : met.bucket_bounds) {
                hist->add_bucket_bounds(bound);
            }
            for (const auto& count : met.bucket_counts) {
                hist->add_bucket_counts(count);
            }
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
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

void MetricsBatchBuilder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

// ============================================================================
// LogBatchBuilder
// ============================================================================

LogBatchBuilder::LogBatchBuilder(const std::string& source_id)
    : source_id_(source_id) {
}

void LogBatchBuilder::add(const vep_OtelLogEntry& msg) {
    PendingLog log;
    log.timestamp_ms = msg.header.timestamp_ns / 1000000;
    log.level = static_cast<int>(msg.level);
    log.component = msg.component ? msg.component : "";
    log.message = msg.message ? msg.message : "";

    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        log.attr_keys.push_back("service");
        log.attr_values.push_back(msg.header.source_id);
    }

    for (uint32_t i = 0; i < msg.attributes._length; ++i) {
        if (msg.attributes._buffer[i].key) {
            log.attr_keys.push_back(msg.attributes._buffer[i].key);
            log.attr_values.push_back(
                msg.attributes._buffer[i].value ? msg.attributes._buffer[i].value : "");
        }
    }

    std::lock_guard<std::mutex> lock(mutex_);
    pending_.push_back(std::move(log));
}

bool LogBatchBuilder::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

size_t LogBatchBuilder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

std::vector<uint8_t> LogBatchBuilder::build() {
    std::vector<PendingLog> logs;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) {
            return {};
        }
        logs = std::move(pending_);
        pending_.clear();
    }

    int64_t base_ts = logs[0].timestamp_ms;

    vep::transfer::LogBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(sequence_++);

    for (const auto& log : logs) {
        auto* pb_log = batch.add_logs();
        pb_log->set_timestamp_delta_ms(
            static_cast<uint32_t>(log.timestamp_ms - base_ts));
        pb_log->set_level(static_cast<vep::transfer::LogLevel>(log.level));
        pb_log->set_component(log.component);
        pb_log->set_message(log.message);

        for (const auto& key : log.attr_keys) {
            pb_log->add_attr_keys(key);
        }
        for (const auto& val : log.attr_values) {
            pb_log->add_attr_values(val);
        }
    }

    std::string serialized;
    batch.SerializeToString(&serialized);
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

void LogBatchBuilder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
}

}  // namespace vep::exporter
