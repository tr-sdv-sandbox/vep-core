// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vss_dbc_parser.hpp"
#include "vss-signal.h"

#include <cstdint>
#include <vector>

namespace vep {

/// @brief Encodes VSS signal values into CAN frame data bytes
///
/// Handles the conversion from VSS typed values to raw CAN signal bits:
/// - Numeric values: Applies inverse transform (raw = (value - offset) / factor)
/// - String enum values: Maps string to integer via value_mapping
/// - Packs the raw value into the CAN frame at the specified bit position
///
/// Uses Intel (little-endian) byte order for bit packing.
class CANEncoder {
public:
    CANEncoder() = default;

    /// @brief Encode a VSS value into CAN frame data
    /// @param mapping The CAN signal mapping with encoding parameters
    /// @param value The VSS value to encode
    /// @param frame_data Output buffer (must be pre-sized to message_length)
    /// @return true if encoding succeeded, false on error
    bool encode_signal(const CANSignalMapping& mapping,
                       const vep_VssValue& value,
                       std::vector<uint8_t>& frame_data);

    /// @brief Get the numeric value from a vep_VssValue
    /// @param value The VSS value
    /// @param out_value Output numeric value
    /// @return true if value could be extracted as numeric
    static bool get_numeric_value(const vep_VssValue& value, double& out_value);

    /// @brief Get the string value from a vep_VssValue
    /// @param value The VSS value
    /// @param out_value Output string value
    /// @return true if value is a string type
    static bool get_string_value(const vep_VssValue& value, std::string& out_value);

private:
    /// @brief Apply inverse transform: raw = (value - offset) / factor
    /// @param vss_value The VSS signal value
    /// @param offset Transform offset
    /// @param factor Transform factor
    /// @return Raw CAN signal value
    static double apply_inverse_transform(double vss_value, double offset, double factor);

    /// @brief Map a string value to integer using value_mapping
    /// @param str_value String value to map
    /// @param mapping The mapping table
    /// @param out_value Output integer value
    /// @return true if mapping found
    static bool map_string_to_int(const std::string& str_value,
                                  const std::map<std::string, int>& mapping,
                                  int& out_value);

    /// @brief Pack a raw value into frame_data at specified bit position
    /// @param frame_data The CAN frame data buffer
    /// @param raw_value Value to pack (as unsigned integer)
    /// @param bit_start Start bit position
    /// @param bit_length Number of bits
    ///
    /// Uses Intel (little-endian) byte order: bit_start is the LSB position,
    /// and bits are packed from LSB to MSB across bytes.
    static void pack_bits(std::vector<uint8_t>& frame_data,
                          uint64_t raw_value,
                          uint16_t bit_start, uint16_t bit_length);
};

}  // namespace vep
