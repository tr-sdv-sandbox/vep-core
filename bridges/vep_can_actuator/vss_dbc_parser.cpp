// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vss_dbc_parser.hpp"

#include <glog/logging.h>
#include <nlohmann/json.hpp>

#include <fstream>

namespace vep {

using json = nlohmann::json;

bool VssDbcParser::load(const std::string& json_path) {
    mappings_.clear();

    std::ifstream file(json_path);
    if (!file.is_open()) {
        LOG(ERROR) << "Failed to open vss_dbc.json: " << json_path;
        return false;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& e) {
        LOG(ERROR) << "JSON parse error in " << json_path << ": " << e.what();
        return false;
    }

    // Start parsing from root (usually "Vehicle")
    parse_node(&root, "");

    LOG(INFO) << "Loaded " << mappings_.size() << " actuator mappings from " << json_path;
    return true;
}

void VssDbcParser::parse_node(const void* node_ptr, const std::string& current_path) {
    const json& node = *static_cast<const json*>(node_ptr);

    if (!node.is_object()) {
        return;
    }

    for (auto& [key, value] : node.items()) {
        if (!value.is_object()) {
            continue;
        }

        // Build the path for this node
        std::string node_path = current_path.empty() ? key : (current_path + "." + key);

        // Check if this is a VSS node with a type
        if (value.contains("type")) {
            std::string type = value["type"].get<std::string>();

            if (type == "branch") {
                // Recurse into children
                if (value.contains("children")) {
                    parse_node(&value["children"], node_path);
                }
            } else if (type == "actuator") {
                // Check if this actuator has a vss2dbc mapping
                if (value.contains("vss2dbc")) {
                    CANSignalMapping mapping;
                    mapping.vss_path = node_path;

                    if (value.contains("datatype")) {
                        mapping.datatype = value["datatype"].get<std::string>();
                    }

                    if (parse_vss2dbc(&value["vss2dbc"], mapping)) {
                        mappings_[node_path] = std::move(mapping);
                        VLOG(1) << "Loaded actuator mapping: " << node_path
                                << " -> CAN ID 0x" << std::hex << mappings_[node_path].can_id;
                    }
                }
            }
            // Ignore "sensor" and other types for this parser
        }
    }
}

bool VssDbcParser::parse_vss2dbc(const void* vss2dbc_ptr, CANSignalMapping& mapping) {
    const json& vss2dbc = *static_cast<const json*>(vss2dbc_ptr);

    if (!vss2dbc.is_object()) {
        LOG(WARNING) << "vss2dbc is not an object for " << mapping.vss_path;
        return false;
    }

    // Parse message info
    if (vss2dbc.contains("message")) {
        const json& msg = vss2dbc["message"];

        if (msg.contains("name")) {
            mapping.message_name = msg["name"].get<std::string>();
        }

        if (msg.contains("canid")) {
            std::string canid_str = msg["canid"].get<std::string>();
            // Parse hex string like "0x1AFFCB02"
            try {
                mapping.can_id = std::stoul(canid_str, nullptr, 0);
            } catch (const std::exception& e) {
                LOG(WARNING) << "Invalid canid format: " << canid_str;
                return false;
            }
        }

        if (msg.contains("length_in_bytes")) {
            mapping.message_length = msg["length_in_bytes"].get<uint8_t>();
        }

        if (msg.contains("cycle_time_ms")) {
            mapping.cycle_time_ms = msg["cycle_time_ms"].get<uint32_t>();
        }
    } else {
        LOG(WARNING) << "vss2dbc missing 'message' for " << mapping.vss_path;
        return false;
    }

    // Parse signal info
    if (vss2dbc.contains("signal")) {
        const json& sig = vss2dbc["signal"];

        if (sig.contains("name")) {
            mapping.signal_name = sig["name"].get<std::string>();
        }

        if (sig.contains("bitposition")) {
            const json& bitpos = sig["bitposition"];
            if (bitpos.contains("start")) {
                mapping.bit_start = bitpos["start"].get<uint16_t>();
            }
            if (bitpos.contains("length")) {
                mapping.bit_length = bitpos["length"].get<uint16_t>();
            }
        }

        // Parse transform
        if (sig.contains("transform")) {
            const json& transform = sig["transform"];

            if (transform.contains("offset")) {
                mapping.offset = transform["offset"].get<double>();
            }
            if (transform.contains("factor")) {
                mapping.factor = transform["factor"].get<double>();
            }

            // String enum mapping
            if (transform.contains("mapping")) {
                const json& value_map = transform["mapping"];
                for (auto& [str_val, int_val] : value_map.items()) {
                    mapping.value_mapping[str_val] = int_val.get<int>();
                }
            }
        }

        if (sig.contains("min")) {
            mapping.min_value = sig["min"].get<double>();
        }
        if (sig.contains("max")) {
            mapping.max_value = sig["max"].get<double>();
        }
    } else {
        LOG(WARNING) << "vss2dbc missing 'signal' for " << mapping.vss_path;
        return false;
    }

    return true;
}

const CANSignalMapping* VssDbcParser::find_mapping(const std::string& vss_path) const {
    auto it = mappings_.find(vss_path);
    if (it != mappings_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::vector<std::string> VssDbcParser::get_actuator_paths() const {
    std::vector<std::string> paths;
    paths.reserve(mappings_.size());
    for (const auto& [path, _] : mappings_) {
        paths.push_back(path);
    }
    return paths;
}

}  // namespace vep
