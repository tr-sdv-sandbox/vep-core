// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file batch_builder.hpp
/// @brief Unified batch building for efficient wire transfer
///
/// Builds TransferBatch protobuf with interleaved items:
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

/// Item type for unified batching
enum class ItemType {
    Signal,
    Event,
    Metric,
    Log
};

/// Builds unified TransferBatch with interleaved items
///
/// Collects all data types (signals, events, metrics, logs)
/// in arrival order. Used for both MQTT and SOME/IP transport.
///
/// Example:
/// @code
///   UnifiedBatchBuilder builder("vep_exporter", 100);
///   builder.add(signal);
///   builder.add(event);
///   builder.add(gauge);
///   auto batch = builder.build();  // Contains all items interleaved
/// @endcode
class UnifiedBatchBuilder {
public:
    /// Create builder with source ID and max items per batch
    explicit UnifiedBatchBuilder(const std::string& source_id = "vep_exporter",
                                  size_t max_items = 100);

    /// @name Add items (all types accepted)
    /// Items are stored in arrival order
    /// @{
    void add(const vep_VssSignal& msg);
    void add(const vep_Event& msg);
    void add(const vep_OtelGauge& msg);
    void add(const vep_OtelCounter& msg);
    void add(const vep_OtelHistogram& msg);
    void add(const vep_OtelLogEntry& msg);
    /// @}

    /// Check if batch has any items
    bool ready() const;

    /// Get current item count
    size_t size() const;

    /// Check if batch is at capacity
    bool full() const;

    /// Build the batch and serialize to bytes
    /// @return Serialized TransferBatch protobuf
    std::vector<uint8_t> build();

    /// Reset the builder for next batch
    void reset();

    /// Get approximate serialized size (for size-based flushing)
    size_t estimated_size() const;

private:
    /// Pending item - stores pre-converted protobuf and timestamp
    struct PendingItem {
        int64_t timestamp_ms;
        ItemType type;
        vep::transfer::TransferItem proto_item;  // Pre-built
    };

    std::string source_id_;
    size_t max_items_;
    std::atomic<uint32_t> sequence_{0};
    mutable std::mutex mutex_;
    std::vector<PendingItem> pending_;
    int64_t base_timestamp_ms_ = 0;
    size_t estimated_bytes_ = 0;
};

}  // namespace vep::exporter
