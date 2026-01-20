// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

namespace vep {

/// @brief CAN signal encoding information from vss2dbc mapping
struct CANSignalMapping {
    std::string vss_path;           // Full VSS path, e.g., "Vehicle.Cabin.HVAC.IsAirConditioningActive"
    std::string datatype;           // VSS datatype: "int8", "string", etc.

    // CAN message info
    std::string message_name;
    uint32_t can_id = 0;            // CAN arbitration ID (e.g., 0x1AFFCB02)
    uint8_t message_length = 8;     // Message length in bytes (default 8)
    uint32_t cycle_time_ms = 0;     // Cycle time in ms (informational)

    // Signal encoding info
    std::string signal_name;
    uint16_t bit_start = 0;         // Start bit position
    uint16_t bit_length = 0;        // Length in bits

    // Transform for numeric types: raw_value = (vss_value - offset) / factor
    double offset = 0.0;
    double factor = 1.0;

    // Transform for string enum types: string -> int mapping
    std::map<std::string, int> value_mapping;

    // Value bounds
    double min_value = 0.0;
    double max_value = 0.0;

    bool has_value_mapping() const { return !value_mapping.empty(); }
};

/// @brief Parser for vss_dbc.json configuration files
///
/// Parses a JSON file that defines VSS actuator paths and their corresponding
/// CAN signal encoding information. The JSON format follows the VSS tree structure
/// with `vss2dbc` nodes containing CAN message and signal definitions.
///
/// Example JSON structure:
/// {
///   "Vehicle": {
///     "type": "branch",
///     "children": {
///       "Cabin": {
///         "type": "branch",
///         "children": {
///           "HVAC": {
///             "type": "branch",
///             "children": {
///               "IsAirConditioningActive": {
///                 "type": "actuator",
///                 "datatype": "int8",
///                 "vss2dbc": {
///                   "message": { "name": "HVAC_Control", "canid": "0x1AFFCB02", ... },
///                   "signal": { "name": "AC_Active", "bitposition": { "start": 0, "length": 8 }, ... }
///                 }
///               }
///             }
///           }
///         }
///       }
///     }
///   }
/// }
class VssDbcParser {
public:
    VssDbcParser() = default;

    /// @brief Load and parse a vss_dbc.json file
    /// @param json_path Path to the JSON configuration file
    /// @return true on success, false on parse error
    bool load(const std::string& json_path);

    /// @brief Find mapping for a VSS actuator path
    /// @param vss_path Full VSS path (e.g., "Vehicle.Cabin.HVAC.IsAirConditioningActive")
    /// @return Pointer to mapping if found, nullptr otherwise
    const CANSignalMapping* find_mapping(const std::string& vss_path) const;

    /// @brief Get all actuator paths that have CAN mappings
    /// @return Vector of VSS paths
    std::vector<std::string> get_actuator_paths() const;

    /// @brief Get number of loaded actuator mappings
    size_t mapping_count() const { return mappings_.size(); }

private:
    /// @brief Recursively parse VSS tree nodes
    /// @param node Current JSON node
    /// @param current_path Current path prefix (dot-separated)
    void parse_node(const void* node, const std::string& current_path);

    /// @brief Parse vss2dbc mapping from a node
    /// @param vss2dbc_node The vss2dbc JSON object
    /// @param mapping Output mapping to populate
    /// @return true on success
    bool parse_vss2dbc(const void* vss2dbc_node, CANSignalMapping& mapping);

    std::unordered_map<std::string, CANSignalMapping> mappings_;
};

}  // namespace vep
