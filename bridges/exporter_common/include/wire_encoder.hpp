// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file wire_encoder.hpp
/// @brief DDS to Protobuf wire format conversion
///
/// Converts DDS IDL types (vep_VssSignal, vep_Event, etc.) to Protobuf
/// types defined in transfer.proto for efficient serialization.

#include "types.h"
#include "vss-signal.h"
#include "events.h"
#include "otel-metrics.h"
#include "otel-logs.h"
#include "diagnostics.h"
#include "transfer.pb.h"

namespace vep::exporter {

/// Convert DDS Quality enum to Protobuf Quality enum
vep::transfer::Quality convert_quality(vep_VssQuality dds_quality);

/// Convert DDS StructValue to Protobuf StructValue
/// Handles nested structs recursively
void convert_struct_value(const vep_VssStructValue& dds_struct,
                          vep::transfer::StructValue* pb_struct);

/// Convert DDS StructField to Protobuf StructField
/// Handles all primitive types, arrays, and nested structs
void convert_struct_field(const vep_VssStructField& dds_field,
                          vep::transfer::StructField* pb_field);

/// Convert DDS Value to Protobuf Signal value
/// Sets the appropriate oneof field based on value type
/// @return true if conversion succeeded, false if type not supported
bool convert_value_to_signal(const vep_VssValue& dds_value,
                             vep::transfer::Signal* pb_signal);

/// Encode a complete VssSignal to protobuf Signal
/// Copies path, timestamp, quality, and value
/// @param msg DDS VssSignal message
/// @param pb_signal Output protobuf Signal
/// @param base_timestamp_ms Base timestamp for delta calculation
void encode_vss_signal(const vep_VssSignal& msg,
                       vep::transfer::Signal* pb_signal,
                       int64_t base_timestamp_ms);

/// Encode an Event to protobuf Event
void encode_event(const vep_Event& msg,
                  vep::transfer::Event* pb_event,
                  int64_t base_timestamp_ms);

/// Encode a Gauge metric to protobuf Metric
void encode_gauge(const vep_OtelGauge& msg,
                  vep::transfer::Metric* pb_metric,
                  int64_t base_timestamp_ms);

/// Encode a Counter metric to protobuf Metric
void encode_counter(const vep_OtelCounter& msg,
                    vep::transfer::Metric* pb_metric,
                    int64_t base_timestamp_ms);

/// Encode a Histogram metric to protobuf Metric
void encode_histogram(const vep_OtelHistogram& msg,
                      vep::transfer::Metric* pb_metric,
                      int64_t base_timestamp_ms);

/// Encode a Log entry to protobuf LogEntry
void encode_log(const vep_OtelLogEntry& msg,
                vep::transfer::LogEntry* pb_log,
                int64_t base_timestamp_ms);

}  // namespace vep::exporter
