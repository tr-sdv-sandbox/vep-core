// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file vss_config.hpp
/// @brief VSS configuration loader - parses VSS JSON spec to extract signal metadata
///
/// Loads VSS 5.1 JSON specification and provides:
/// - List of all sensor paths
/// - List of all actuator paths
/// - Datatype information for each signal
///
/// Used by kuksa_dds_bridge to dynamically configure which signals to bridge.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace bridge {

/// VSS signal types
enum class SignalType {
    SENSOR,
    ACTUATOR,
    ATTRIBUTE,
    BRANCH
};

/// VSS data types
enum class DataType {
    BOOLEAN,
    INT8,
    INT16,
    INT32,
    INT64,
    UINT8,
    UINT16,
    UINT32,
    UINT64,
    FLOAT,
    DOUBLE,
    STRING,
    UNKNOWN
};

/// Signal metadata extracted from VSS spec
struct SignalInfo {
    std::string path;
    SignalType type;
    DataType datatype;
    std::string description;
    std::optional<std::string> unit;
};

/// VSS configuration loaded from JSON specification
class VssConfig {
public:
    /// Load VSS configuration from JSON file
    /// @param json_path Path to VSS JSON spec (e.g., vss-5.1.json)
    /// @return true if loaded successfully
    bool load(const std::string& json_path);

    /// Get all sensor signals
    const std::vector<SignalInfo>& sensors() const { return sensors_; }

    /// Get all actuator signals
    const std::vector<SignalInfo>& actuators() const { return actuators_; }

    /// Get all signals (sensors + actuators + attributes)
    const std::vector<SignalInfo>& all_signals() const { return all_signals_; }

    /// Lookup signal info by path
    /// @return nullptr if not found
    const SignalInfo* find(const std::string& path) const;

    /// Check if a path is an actuator
    bool is_actuator(const std::string& path) const;

    /// Check if a path is a sensor
    bool is_sensor(const std::string& path) const;

    /// Filter signals matching a path prefix
    std::vector<SignalInfo> filter_by_prefix(const std::string& prefix) const;

private:
    void parse_node(const nlohmann::json& node, const std::string& prefix);
    static DataType parse_datatype(const std::string& dtype);
    static SignalType parse_signal_type(const std::string& type);

    std::vector<SignalInfo> sensors_;
    std::vector<SignalInfo> actuators_;
    std::vector<SignalInfo> all_signals_;
    std::unordered_map<std::string, SignalInfo> by_path_;
};

}  // namespace bridge
