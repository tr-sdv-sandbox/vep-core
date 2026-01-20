// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "can_encoder.hpp"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace vep {

bool CANEncoder::encode_signal(const CANSignalMapping& mapping,
                               const vep_VssValue& value,
                               std::vector<uint8_t>& frame_data) {
    // Ensure frame_data is properly sized
    if (frame_data.size() < mapping.message_length) {
        frame_data.resize(mapping.message_length, 0);
    }

    uint64_t raw_value = 0;

    // Handle string enum mapping
    if (mapping.has_value_mapping()) {
        std::string str_value;
        if (!get_string_value(value, str_value)) {
            LOG(WARNING) << "Expected string value for " << mapping.vss_path
                         << " but got different type";
            return false;
        }

        int int_value = 0;
        if (!map_string_to_int(str_value, mapping.value_mapping, int_value)) {
            LOG(WARNING) << "Unknown string value '" << str_value
                         << "' for " << mapping.vss_path;
            return false;
        }
        raw_value = static_cast<uint64_t>(int_value);
    } else {
        // Numeric transform
        double numeric_value = 0.0;
        if (!get_numeric_value(value, numeric_value)) {
            LOG(WARNING) << "Could not extract numeric value for " << mapping.vss_path;
            return false;
        }

        double raw_double = apply_inverse_transform(numeric_value, mapping.offset, mapping.factor);

        // Clamp to valid range if specified
        if (mapping.min_value != mapping.max_value) {
            double raw_min = apply_inverse_transform(mapping.min_value, mapping.offset, mapping.factor);
            double raw_max = apply_inverse_transform(mapping.max_value, mapping.offset, mapping.factor);
            if (raw_min > raw_max) std::swap(raw_min, raw_max);
            raw_double = std::clamp(raw_double, raw_min, raw_max);
        }

        // Round to nearest integer
        raw_double = std::round(raw_double);

        // Clamp to bit range
        uint64_t max_for_bits = (1ULL << mapping.bit_length) - 1;
        if (raw_double < 0) {
            raw_value = 0;
        } else if (raw_double > static_cast<double>(max_for_bits)) {
            raw_value = max_for_bits;
        } else {
            raw_value = static_cast<uint64_t>(raw_double);
        }
    }

    pack_bits(frame_data, raw_value, mapping.bit_start, mapping.bit_length);

    VLOG(2) << "Encoded " << mapping.vss_path << " = " << raw_value
            << " at bit " << mapping.bit_start << " len " << mapping.bit_length;

    return true;
}

bool CANEncoder::get_numeric_value(const vep_VssValue& value, double& out_value) {
    switch (value.type) {
        case vep_VSS_VALUE_TYPE_BOOL:
            out_value = value.bool_value ? 1.0 : 0.0;
            return true;
        case vep_VSS_VALUE_TYPE_INT8:
            out_value = static_cast<double>(static_cast<int8_t>(value.int8_value));
            return true;
        case vep_VSS_VALUE_TYPE_INT16:
            out_value = static_cast<double>(value.int16_value);
            return true;
        case vep_VSS_VALUE_TYPE_INT32:
            out_value = static_cast<double>(value.int32_value);
            return true;
        case vep_VSS_VALUE_TYPE_INT64:
            out_value = static_cast<double>(value.int64_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT8:
            out_value = static_cast<double>(value.uint8_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT16:
            out_value = static_cast<double>(value.uint16_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT32:
            out_value = static_cast<double>(value.uint32_value);
            return true;
        case vep_VSS_VALUE_TYPE_UINT64:
            out_value = static_cast<double>(value.uint64_value);
            return true;
        case vep_VSS_VALUE_TYPE_FLOAT:
            out_value = static_cast<double>(value.float_value);
            return true;
        case vep_VSS_VALUE_TYPE_DOUBLE:
            out_value = value.double_value;
            return true;
        default:
            return false;
    }
}

bool CANEncoder::get_string_value(const vep_VssValue& value, std::string& out_value) {
    if (value.type == vep_VSS_VALUE_TYPE_STRING && value.string_value != nullptr) {
        out_value = value.string_value;
        return true;
    }
    return false;
}

double CANEncoder::apply_inverse_transform(double vss_value, double offset, double factor) {
    // Forward transform (DBC->VSS): vss_value = raw * factor + offset
    // Inverse transform (VSS->DBC): raw = (vss_value - offset) / factor
    if (factor == 0.0) {
        LOG(WARNING) << "Factor is zero, cannot apply inverse transform";
        return 0.0;
    }
    return (vss_value - offset) / factor;
}

bool CANEncoder::map_string_to_int(const std::string& str_value,
                                   const std::map<std::string, int>& mapping,
                                   int& out_value) {
    auto it = mapping.find(str_value);
    if (it != mapping.end()) {
        out_value = it->second;
        return true;
    }
    return false;
}

void CANEncoder::pack_bits(std::vector<uint8_t>& frame_data,
                           uint64_t raw_value,
                           uint16_t bit_start, uint16_t bit_length) {
    // Intel (little-endian) byte order:
    // bit_start is the position of the LSB in the frame
    // Bits are packed from LSB to MSB

    for (uint16_t i = 0; i < bit_length; ++i) {
        uint16_t bit_pos = bit_start + i;
        uint16_t byte_idx = bit_pos / 8;
        uint16_t bit_in_byte = bit_pos % 8;

        if (byte_idx >= frame_data.size()) {
            break;
        }

        // Extract bit i from raw_value
        uint8_t bit = (raw_value >> i) & 1;

        // Set or clear the bit in frame_data
        if (bit) {
            frame_data[byte_idx] |= (1 << bit_in_byte);
        } else {
            frame_data[byte_idx] &= ~(1 << bit_in_byte);
        }
    }
}

}  // namespace vep
