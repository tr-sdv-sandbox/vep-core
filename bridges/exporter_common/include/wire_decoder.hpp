// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file wire_decoder.hpp
/// @brief Protobuf wire format to structured data conversion
///
/// Decodes TransferBatch protobuf back to structured C++ types
/// for use in receivers, testing, and cloud processing.
///
/// Unlike the encoder which works with DDS C types, the decoder produces
/// C++ friendly types that own their memory (std::string, std::vector).

#include "transfer.pb.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace vep::exporter {

// =============================================================================
// Decoded Types (C++ friendly, memory-owning)
// =============================================================================

/// Signal quality indicator
enum class DecodedQuality {
    UNKNOWN,
    VALID,
    INVALID,
    NOT_AVAILABLE
};

/// A decoded struct field value
struct DecodedStructField;

/// A decoded struct value
struct DecodedStruct {
    std::string type_name;
    std::vector<DecodedStructField> fields;
};

/// Variant type for all possible signal values
using DecodedValue = std::variant<
    std::monostate,           // Empty
    bool,                     // Bool
    int8_t, int16_t, int32_t, int64_t,      // Signed integers
    uint8_t, uint16_t, uint32_t, uint64_t,  // Unsigned integers
    float, double,            // Floating point
    std::string,              // String
    std::vector<bool>,        // Bool array
    std::vector<int8_t>, std::vector<int16_t>, std::vector<int32_t>, std::vector<int64_t>,
    std::vector<uint8_t>, std::vector<uint16_t>, std::vector<uint32_t>, std::vector<uint64_t>,
    std::vector<float>, std::vector<double>,
    std::vector<std::string>, // String array
    DecodedStruct,            // Struct
    std::vector<DecodedStruct> // Struct array
>;

/// A decoded struct field
struct DecodedStructField {
    std::string name;
    DecodedValue value;
};

/// A decoded VSS signal
struct DecodedSignal {
    std::string path;
    int64_t timestamp_ms;      // Absolute timestamp (base + delta)
    DecodedQuality quality;
    DecodedValue value;
};

/// A decoded event
struct DecodedEvent {
    std::string event_id;
    int64_t timestamp_ms;
    std::string category;
    std::string event_type;
    int32_t severity;
    std::vector<uint8_t> payload;
};

/// Metric type enumeration
enum class MetricType {
    GAUGE,
    COUNTER,
    HISTOGRAM
};

/// A decoded metric
struct DecodedMetric {
    std::string name;
    int64_t timestamp_ms;
    MetricType type;
    double value;
    std::map<std::string, std::string> labels;

    // Histogram-specific fields
    uint64_t sample_count = 0;
    double sample_sum = 0.0;
    std::vector<double> bucket_bounds;
    std::vector<uint64_t> bucket_counts;
};

/// Log level enumeration
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3
};

/// A decoded log entry
struct DecodedLogEntry {
    int64_t timestamp_ms;
    LogLevel level;
    std::string component;
    std::string message;
    std::map<std::string, std::string> attributes;
};

/// Item type in a transfer batch
enum class DecodedItemType {
    SIGNAL,
    EVENT,
    METRIC,
    LOG,
    UNKNOWN
};

/// A decoded transfer batch item
struct DecodedItem {
    int64_t timestamp_ms;
    DecodedItemType type;

    // Only one of these is populated based on type
    std::optional<DecodedSignal> signal;
    std::optional<DecodedEvent> event;
    std::optional<DecodedMetric> metric;
    std::optional<DecodedLogEntry> log;
};

/// A decoded unified transfer batch
struct DecodedTransferBatch {
    std::string source_id;
    uint32_t sequence;
    int64_t base_timestamp_ms;
    std::vector<DecodedItem> items;

    /// Count items of each type
    size_t signal_count() const;
    size_t event_count() const;
    size_t metric_count() const;
    size_t log_count() const;
};

// =============================================================================
// Decoder Functions
// =============================================================================

/// Convert Protobuf Quality to decoded quality
DecodedQuality decode_quality(vep::transfer::Quality pb_quality);

/// Decode a Protobuf Signal to DecodedSignal
/// @param pb_signal Protobuf signal
/// @param timestamp_ms Absolute timestamp for this item
/// @return Decoded signal
DecodedSignal decode_signal(const vep::transfer::Signal& pb_signal,
                            int64_t timestamp_ms);

/// Decode a Protobuf Event to DecodedEvent
DecodedEvent decode_event(const vep::transfer::Event& pb_event,
                          int64_t timestamp_ms);

/// Decode a Protobuf Metric to DecodedMetric
DecodedMetric decode_metric(const vep::transfer::Metric& pb_metric,
                            int64_t timestamp_ms);

/// Decode a Protobuf LogEntry to DecodedLogEntry
DecodedLogEntry decode_log(const vep::transfer::LogEntry& pb_log,
                           int64_t timestamp_ms);

/// Decode a complete TransferBatch
/// @param data Serialized protobuf bytes
/// @return Decoded batch, or nullopt if parsing failed
std::optional<DecodedTransferBatch> decode_transfer_batch(const std::vector<uint8_t>& data);

// =============================================================================
// Utility Functions
// =============================================================================

/// Get string representation of quality
const char* quality_to_string(DecodedQuality quality);

/// Get string representation of metric type
const char* metric_type_to_string(MetricType type);

/// Get string representation of log level
const char* log_level_to_string(LogLevel level);

/// Get string representation of item type
const char* item_type_to_string(DecodedItemType type);

/// Get the value type name from a DecodedValue
std::string value_type_name(const DecodedValue& value);

/// Convert DecodedValue to string for display
std::string value_to_string(const DecodedValue& value);

}  // namespace vep::exporter
