// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file batch_builder.hpp
/// @brief Batch building for efficient wire transfer
///
/// Builds protobuf batches from DDS messages with:
/// - Delta timestamps (base + per-message offset)
/// - Sequence numbers for ordering
/// - Pre-conversion of values to avoid DDS pointer issues

#include "wire_encoder.hpp"
#include "transfer.pb.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace vep::exporter {

/// Configuration for batch building
struct BatchConfig {
    std::string source_id = "vdr";
    size_t max_signals = 100;
    size_t max_events = 50;
    size_t max_metrics = 100;
    size_t max_logs = 100;
};

/// Builds batches of VSS signals
class SignalBatchBuilder {
public:
    explicit SignalBatchBuilder(const std::string& source_id = "vdr");

    /// Add a signal to the batch
    /// @note Call from DDS callback - copies data immediately
    void add(const vep_VssSignal& msg);

    /// Check if batch is ready (has messages)
    bool ready() const;

    /// Get current batch size
    size_t size() const;

    /// Build the batch and serialize to bytes
    /// @return Serialized protobuf batch
    std::vector<uint8_t> build();

    /// Reset the builder for next batch
    void reset();

private:
    struct PendingSignal {
        std::string path;
        int64_t timestamp_ms;
        vep::transfer::Quality quality;
        vep::transfer::Signal proto_signal;  // Pre-converted
    };

    std::string source_id_;
    std::atomic<uint32_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<PendingSignal> pending_;
    int64_t base_timestamp_ms_ = 0;
};

/// Builds batches of events
class EventBatchBuilder {
public:
    explicit EventBatchBuilder(const std::string& source_id = "vdr");

    void add(const vep_Event& msg);
    bool ready() const;
    size_t size() const;
    std::vector<uint8_t> build();
    void reset();

private:
    struct PendingEvent {
        std::string event_id;
        int64_t timestamp_ms;
        std::string category;
        std::string event_type;
        int severity;
    };

    std::string source_id_;
    std::atomic<uint32_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<PendingEvent> pending_;
};

/// Builds batches of metrics (gauges, counters, histograms)
class MetricsBatchBuilder {
public:
    explicit MetricsBatchBuilder(const std::string& source_id = "vdr");

    void add(const vep_OtelGauge& msg);
    void add(const vep_OtelCounter& msg);
    void add(const vep_OtelHistogram& msg);
    bool ready() const;
    size_t size() const;
    std::vector<uint8_t> build();
    void reset();

private:
    struct PendingMetric {
        std::string name;
        int64_t timestamp_ms;
        int metric_type;  // 0=gauge, 1=counter, 2=histogram
        double value;
        std::vector<std::string> label_keys;
        std::vector<std::string> label_values;
        // Histogram-specific
        uint64_t sample_count = 0;
        double sample_sum = 0.0;
        std::vector<double> bucket_bounds;
        std::vector<uint64_t> bucket_counts;
    };

    std::string source_id_;
    std::atomic<uint32_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<PendingMetric> pending_;
};

/// Builds batches of log entries
class LogBatchBuilder {
public:
    explicit LogBatchBuilder(const std::string& source_id = "vdr");

    void add(const vep_OtelLogEntry& msg);
    bool ready() const;
    size_t size() const;
    std::vector<uint8_t> build();
    void reset();

private:
    struct PendingLog {
        int64_t timestamp_ms;
        int level;
        std::string component;
        std::string message;
        std::vector<std::string> attr_keys;
        std::vector<std::string> attr_values;
    };

    std::string source_id_;
    std::atomic<uint32_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<PendingLog> pending_;
};

}  // namespace vep::exporter
