// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file dds_proto_conversion.hpp
/// @brief Conversion utilities from DDS IDL types to Protobuf types
///
/// Provides functions to convert vss_types (DDS) to vep::transfer (Protobuf)
/// for efficient serialization and transport.

#include "types.h"
#include "vss-signal.h"
#include "transfer.pb.h"

namespace integration {

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
/// Returns true if conversion succeeded, false if type not supported
bool convert_value_to_signal(const vep_VssValue& dds_value,
                             vep::transfer::Signal* pb_signal);

/// Convert DDS Quality enum to Protobuf Quality enum
vep::transfer::Quality convert_quality(vep_VssQuality dds_quality);

}  // namespace integration
