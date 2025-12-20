// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "batch_builder.hpp"

namespace vep::exporter {

// ============================================================================
// UnifiedBatchBuilder
// ============================================================================

UnifiedBatchBuilder::UnifiedBatchBuilder(const std::string& source_id, size_t max_items)
    : source_id_(source_id)
    , max_items_(max_items) {
}

void UnifiedBatchBuilder::add(const vep_VssSignal& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Signal;

    // Build the signal proto
    auto* signal = item.proto_item.mutable_signal();
    signal->set_path(msg.path ? msg.path : "");
    signal->set_quality(convert_quality(msg.quality));

    // Convert value
    vep::transfer::Signal temp_signal;
    convert_value_to_signal(msg.value, &temp_signal);
    signal->MergeFrom(temp_signal);

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

void UnifiedBatchBuilder::add(const vep_Event& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Event;

    // Build the event proto
    auto* event = item.proto_item.mutable_event();
    event->set_event_id(msg.event_id ? msg.event_id : "");
    event->set_category(msg.category ? msg.category : "");
    event->set_event_type(msg.event_type ? msg.event_type : "");
    event->set_severity(static_cast<vep::transfer::Severity>(msg.severity));

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

void UnifiedBatchBuilder::add(const vep_OtelGauge& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Metric;

    // Build the metric proto
    auto* metric = item.proto_item.mutable_metric();
    metric->set_name(msg.name ? msg.name : "");
    metric->set_gauge(msg.value);

    // Add labels
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        metric->add_label_keys("service");
        metric->add_label_values(msg.header.source_id);
    }
    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            metric->add_label_keys(msg.labels._buffer[i].key);
            metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

void UnifiedBatchBuilder::add(const vep_OtelCounter& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Metric;

    // Build the metric proto
    auto* metric = item.proto_item.mutable_metric();
    metric->set_name(msg.name ? msg.name : "");
    metric->set_counter(msg.value);

    // Add labels
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        metric->add_label_keys("service");
        metric->add_label_values(msg.header.source_id);
    }
    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            metric->add_label_keys(msg.labels._buffer[i].key);
            metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

void UnifiedBatchBuilder::add(const vep_OtelHistogram& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Metric;

    // Build the metric proto
    auto* metric = item.proto_item.mutable_metric();
    metric->set_name(msg.name ? msg.name : "");

    auto* hist = metric->mutable_histogram();
    hist->set_sample_count(msg.sample_count);
    hist->set_sample_sum(msg.sample_sum);
    for (uint32_t i = 0; i < msg.buckets._length; ++i) {
        hist->add_bucket_bounds(msg.buckets._buffer[i].upper_bound);
        hist->add_bucket_counts(msg.buckets._buffer[i].cumulative_count);
    }

    // Add labels
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        metric->add_label_keys("service");
        metric->add_label_values(msg.header.source_id);
    }
    for (uint32_t i = 0; i < msg.labels._length; ++i) {
        if (msg.labels._buffer[i].key) {
            metric->add_label_keys(msg.labels._buffer[i].key);
            metric->add_label_values(
                msg.labels._buffer[i].value ? msg.labels._buffer[i].value : "");
        }
    }

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

void UnifiedBatchBuilder::add(const vep_OtelLogEntry& msg) {
    PendingItem item;
    item.timestamp_ms = msg.header.timestamp_ns / 1000000;
    item.type = ItemType::Log;

    // Build the log proto
    auto* log = item.proto_item.mutable_log();
    log->set_level(static_cast<vep::transfer::LogLevel>(msg.level));
    log->set_component(msg.component ? msg.component : "");
    log->set_message(msg.message ? msg.message : "");

    // Add attributes
    if (msg.header.source_id && msg.header.source_id[0] != '\0') {
        log->add_attr_keys("service");
        log->add_attr_values(msg.header.source_id);
    }
    for (uint32_t i = 0; i < msg.attributes._length; ++i) {
        if (msg.attributes._buffer[i].key) {
            log->add_attr_keys(msg.attributes._buffer[i].key);
            log->add_attr_values(
                msg.attributes._buffer[i].value ? msg.attributes._buffer[i].value : "");
        }
    }

    size_t item_size = item.proto_item.ByteSizeLong();

    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_.empty()) {
        base_timestamp_ms_ = item.timestamp_ms;
    }
    pending_.push_back(std::move(item));
    estimated_bytes_ += item_size;
}

bool UnifiedBatchBuilder::ready() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !pending_.empty();
}

size_t UnifiedBatchBuilder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size();
}

bool UnifiedBatchBuilder::full() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_.size() >= max_items_;
}

size_t UnifiedBatchBuilder::estimated_size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return estimated_bytes_;
}

std::vector<uint8_t> UnifiedBatchBuilder::build() {
    std::vector<PendingItem> items;
    int64_t base_ts;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty()) {
            return {};
        }
        items = std::move(pending_);
        pending_.clear();
        base_ts = base_timestamp_ms_;
        estimated_bytes_ = 0;
    }

    vep::transfer::TransferBatch batch;
    batch.set_base_timestamp_ms(base_ts);
    batch.set_source_id(source_id_);
    batch.set_sequence(sequence_++);

    for (auto& item : items) {
        auto* pb_item = batch.add_items();

        // Set timestamp delta
        pb_item->set_timestamp_delta_ms(
            static_cast<uint32_t>(item.timestamp_ms - base_ts));

        // Move the pre-built item data
        pb_item->MergeFrom(item.proto_item);
    }

    std::string serialized;
    batch.SerializeToString(&serialized);
    return std::vector<uint8_t>(serialized.begin(), serialized.end());
}

void UnifiedBatchBuilder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_.clear();
    base_timestamp_ms_ = 0;
    estimated_bytes_ = 0;
}

}  // namespace vep::exporter
