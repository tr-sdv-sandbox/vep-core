// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file wire_decoder.hpp
/// @brief Protobuf wire format to structured data conversion
///
/// Decodes Protobuf types from transfer.proto back to structured C++ types
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
    std::map<std::string, std::string> attributes;
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
    std::string trace_id;
    std::string span_id;
};

/// A decoded signal batch
struct DecodedSignalBatch {
    std::string source_id;
    uint32_t sequence;
    int64_t base_timestamp_ms;
    std::vector<DecodedSignal> signals;
};

/// A decoded event batch
struct DecodedEventBatch {
    std::string source_id;
    uint32_t sequence;
    std::vector<DecodedEvent> events;
};

/// A decoded metrics batch
struct DecodedMetricsBatch {
    std::string source_id;
    uint32_t sequence;
    std::vector<DecodedMetric> metrics;
};

/// A decoded log batch
struct DecodedLogBatch {
    std::string source_id;
    uint32_t sequence;
    std::vector<DecodedLogEntry> entries;
};

// =============================================================================
// Decoder Functions
// =============================================================================

/// Convert Protobuf Quality to decoded quality
DecodedQuality decode_quality(vep::transfer::Quality pb_quality);

/// Decode a Protobuf Signal to DecodedSignal
/// @param pb_signal Protobuf signal
/// @param base_timestamp_ms Base timestamp from batch header
/// @return Decoded signal with absolute timestamp
DecodedSignal decode_signal(const vep::transfer::Signal& pb_signal,
                            int64_t base_timestamp_ms);

/// Decode a Protobuf Event to DecodedEvent
DecodedEvent decode_event(const vep::transfer::Event& pb_event,
                          int64_t base_timestamp_ms);

/// Decode a Protobuf Metric to DecodedMetric
DecodedMetric decode_metric(const vep::transfer::Metric& pb_metric,
                            int64_t base_timestamp_ms);

/// Decode a Protobuf LogEntry to DecodedLogEntry
DecodedLogEntry decode_log(const vep::transfer::LogEntry& pb_log,
                           int64_t base_timestamp_ms);

/// Decode a complete SignalBatch
/// @param data Serialized protobuf bytes
/// @return Decoded batch, or nullopt if parsing failed
std::optional<DecodedSignalBatch> decode_signal_batch(const std::vector<uint8_t>& data);

/// Decode a complete EventBatch
std::optional<DecodedEventBatch> decode_event_batch(const std::vector<uint8_t>& data);

/// Decode a complete MetricsBatch
std::optional<DecodedMetricsBatch> decode_metrics_batch(const std::vector<uint8_t>& data);

/// Decode a complete LogBatch
std::optional<DecodedLogBatch> decode_log_batch(const std::vector<uint8_t>& data);

// =============================================================================
// Utility Functions
// =============================================================================

/// Get string representation of quality
const char* quality_to_string(DecodedQuality quality);

/// Get string representation of metric type
const char* metric_type_to_string(MetricType type);

/// Get string representation of log level
const char* log_level_to_string(LogLevel level);

/// Get the value type name from a DecodedValue
std::string value_type_name(const DecodedValue& value);

/// Convert DecodedValue to string for display
std::string value_to_string(const DecodedValue& value);

}  // namespace vep::exporter
