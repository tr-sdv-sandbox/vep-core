// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vss_config.hpp"

#include <glog/logging.h>
#include <fstream>

namespace bridge {

bool VssConfig::load(const std::string& json_path) {
    std::ifstream file(json_path);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open VSS config: " << json_path;
        return false;
    }

    try {
        nlohmann::json root = nlohmann::json::parse(file);

        // VSS JSON has "Vehicle" as root
        if (!root.contains("Vehicle")) {
            LOG(ERROR) << "Invalid VSS JSON - missing 'Vehicle' root";
            return false;
        }

        parse_node(root["Vehicle"], "Vehicle");

        LOG(INFO) << "Loaded VSS config: " << sensors_.size() << " sensors, "
                  << actuators_.size() << " actuators, "
                  << all_signals_.size() << " total signals";

        return true;
    } catch (const nlohmann::json::exception& e) {
        LOG(ERROR) << "Failed to parse VSS JSON: " << e.what();
        return false;
    }
}

void VssConfig::parse_node(const nlohmann::json& node, const std::string& prefix) {
    // Check if this is a leaf node (sensor/actuator/attribute)
    if (node.contains("type")) {
        std::string type_str = node["type"].get<std::string>();
        SignalType signal_type = parse_signal_type(type_str);

        if (signal_type == SignalType::BRANCH) {
            // Branch node - recurse into children
            if (node.contains("children")) {
                for (auto& [name, child] : node["children"].items()) {
                    parse_node(child, prefix + "." + name);
                }
            }
        } else {
            // Leaf node - extract signal info
            SignalInfo info;
            info.path = prefix;
            info.type = signal_type;

            if (node.contains("datatype")) {
                info.datatype = parse_datatype(node["datatype"].get<std::string>());
            } else {
                info.datatype = DataType::UNKNOWN;
            }

            if (node.contains("description")) {
                info.description = node["description"].get<std::string>();
            }

            if (node.contains("unit")) {
                info.unit = node["unit"].get<std::string>();
            }

            // Add to appropriate lists
            all_signals_.push_back(info);
            by_path_[info.path] = info;

            if (signal_type == SignalType::SENSOR) {
                sensors_.push_back(info);
            } else if (signal_type == SignalType::ACTUATOR) {
                actuators_.push_back(info);
            }
        }
    } else if (node.contains("children")) {
        // Node without explicit type but has children - recurse
        for (auto& [name, child] : node["children"].items()) {
            parse_node(child, prefix + "." + name);
        }
    }
}

DataType VssConfig::parse_datatype(const std::string& dtype) {
    if (dtype == "boolean") return DataType::BOOLEAN;
    if (dtype == "int8") return DataType::INT8;
    if (dtype == "int16") return DataType::INT16;
    if (dtype == "int32") return DataType::INT32;
    if (dtype == "int64") return DataType::INT64;
    if (dtype == "uint8") return DataType::UINT8;
    if (dtype == "uint16") return DataType::UINT16;
    if (dtype == "uint32") return DataType::UINT32;
    if (dtype == "uint64") return DataType::UINT64;
    if (dtype == "float") return DataType::FLOAT;
    if (dtype == "double") return DataType::DOUBLE;
    if (dtype == "string") return DataType::STRING;
    // Handle arrays - for now treat as unknown
    if (dtype.find("[]") != std::string::npos) return DataType::UNKNOWN;
    return DataType::UNKNOWN;
}

SignalType VssConfig::parse_signal_type(const std::string& type) {
    if (type == "sensor") return SignalType::SENSOR;
    if (type == "actuator") return SignalType::ACTUATOR;
    if (type == "attribute") return SignalType::ATTRIBUTE;
    if (type == "branch") return SignalType::BRANCH;
    return SignalType::BRANCH;  // Default to branch if unknown
}

const SignalInfo* VssConfig::find(const std::string& path) const {
    auto it = by_path_.find(path);
    return it != by_path_.end() ? &it->second : nullptr;
}

bool VssConfig::is_actuator(const std::string& path) const {
    const auto* info = find(path);
    return info && info->type == SignalType::ACTUATOR;
}

bool VssConfig::is_sensor(const std::string& path) const {
    const auto* info = find(path);
    return info && info->type == SignalType::SENSOR;
}

std::vector<SignalInfo> VssConfig::filter_by_prefix(const std::string& prefix) const {
    std::vector<SignalInfo> result;
    for (const auto& sig : all_signals_) {
        if (sig.path.rfind(prefix, 0) == 0) {  // starts with prefix
            result.push_back(sig);
        }
    }
    return result;
}

}  // namespace bridge
