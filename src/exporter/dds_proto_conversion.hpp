// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file dds_proto_conversion.hpp
/// @brief Conversion utilities from DDS IDL types to Protobuf types
///
/// Provides functions to convert vss_types (DDS) to vdr::transfer (Protobuf)
/// for efficient serialization and transport.

#include "vss_types.h"
#include "transfer.pb.h"

namespace integration {

/// Convert DDS StructValue to Protobuf StructValue
/// Handles nested structs recursively
void convert_struct_value(const vss_types_StructValue& dds_struct,
                          vdr::transfer::StructValue* pb_struct);

/// Convert DDS StructField to Protobuf StructField
/// Handles all primitive types, arrays, and nested structs
void convert_struct_field(const vss_types_StructField& dds_field,
                          vdr::transfer::StructField* pb_field);

/// Convert DDS Value to Protobuf Signal value
/// Sets the appropriate oneof field based on value type
/// Returns true if conversion succeeded, false if type not supported
bool convert_value_to_signal(const vss_types_Value& dds_value,
                             vdr::transfer::Signal* pb_signal);

/// Convert DDS Quality enum to Protobuf Quality enum
vdr::transfer::Quality convert_quality(vss_types_Quality dds_quality);

}  // namespace integration
