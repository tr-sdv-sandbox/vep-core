// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "kuksa_dds_bridge.hpp"

#include <glog/logging.h>
#include <chrono>

namespace bridge {

namespace {

// Convert DDS vep_VssValue to kuksa vss::types::Value
vss::types::Value dds_to_kuksa_value(const vep_VssValue& dds_value) {
    switch (dds_value.type) {
        // Primitives
        case vep_VSS_VALUE_TYPE_BOOL:
            return vss::types::Value{dds_value.bool_value};
        case vep_VSS_VALUE_TYPE_INT8:
            return vss::types::Value{static_cast<int8_t>(dds_value.int8_value)};
        case vep_VSS_VALUE_TYPE_INT16:
            return vss::types::Value{dds_value.int16_value};
        case vep_VSS_VALUE_TYPE_INT32:
            return vss::types::Value{dds_value.int32_value};
        case vep_VSS_VALUE_TYPE_INT64:
            return vss::types::Value{dds_value.int64_value};
        case vep_VSS_VALUE_TYPE_UINT8:
            return vss::types::Value{dds_value.uint8_value};
        case vep_VSS_VALUE_TYPE_UINT16:
            return vss::types::Value{dds_value.uint16_value};
        case vep_VSS_VALUE_TYPE_UINT32:
            return vss::types::Value{dds_value.uint32_value};
        case vep_VSS_VALUE_TYPE_UINT64:
            return vss::types::Value{dds_value.uint64_value};
        case vep_VSS_VALUE_TYPE_FLOAT:
            return vss::types::Value{dds_value.float_value};
        case vep_VSS_VALUE_TYPE_DOUBLE:
            return vss::types::Value{dds_value.double_value};
        case vep_VSS_VALUE_TYPE_STRING:
            return vss::types::Value{std::string(dds_value.string_value ? dds_value.string_value : "")};

        // Arrays - convert DDS sequences to std::vector
        case vep_VSS_VALUE_TYPE_BOOL_ARRAY: {
            std::vector<bool> arr;
            arr.reserve(dds_value.bool_array._length);
            for (uint32_t i = 0; i < dds_value.bool_array._length; ++i) {
                arr.push_back(dds_value.bool_array._buffer[i]);
            }
            return vss::types::Value{std::move(arr)};
        }
        case vep_VSS_VALUE_TYPE_INT32_ARRAY: {
            std::vector<int32_t> arr(dds_value.int32_array._buffer,
                dds_value.int32_array._buffer + dds_value.int32_array._length);
            return vss::types::Value{std::move(arr)};
        }
        case vep_VSS_VALUE_TYPE_INT64_ARRAY: {
            std::vector<int64_t> arr(dds_value.int64_array._buffer,
                dds_value.int64_array._buffer + dds_value.int64_array._length);
            return vss::types::Value{std::move(arr)};
        }
        case vep_VSS_VALUE_TYPE_FLOAT_ARRAY: {
            std::vector<float> arr(dds_value.float_array._buffer,
                dds_value.float_array._buffer + dds_value.float_array._length);
            return vss::types::Value{std::move(arr)};
        }
        case vep_VSS_VALUE_TYPE_DOUBLE_ARRAY: {
            std::vector<double> arr(dds_value.double_array._buffer,
                dds_value.double_array._buffer + dds_value.double_array._length);
            return vss::types::Value{std::move(arr)};
        }

        case vep_VSS_VALUE_TYPE_EMPTY:
        default:
            LOG_FIRST_N(WARNING, 1) << "Unsupported value type in dds_to_kuksa_value: " << dds_value.type;
            return vss::types::Value{std::monostate{}};
    }
}

// Convert kuksa Value to DDS vep_VssValue
void kuksa_to_dds_value(const vss::types::Value& value, vep_VssValue& dds_value) {
    // Initialize to empty
    dds_value = {};
    dds_value.type = vep_VSS_VALUE_TYPE_EMPTY;

    std::visit([&dds_value](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            dds_value.type = vep_VSS_VALUE_TYPE_BOOL;
            dds_value.bool_value = v;
        } else if constexpr (std::is_same_v<T, int8_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_INT8;
            dds_value.int8_value = static_cast<uint8_t>(v);
        } else if constexpr (std::is_same_v<T, int16_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_INT16;
            dds_value.int16_value = v;
        } else if constexpr (std::is_same_v<T, int32_t> || std::is_same_v<T, int>) {
            dds_value.type = vep_VSS_VALUE_TYPE_INT32;
            dds_value.int32_value = v;
        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, long>) {
            dds_value.type = vep_VSS_VALUE_TYPE_INT64;
            dds_value.int64_value = v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_UINT8;
            dds_value.uint8_value = v;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_UINT16;
            dds_value.uint16_value = v;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_UINT32;
            dds_value.uint32_value = v;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            dds_value.type = vep_VSS_VALUE_TYPE_UINT64;
            dds_value.uint64_value = v;
        } else if constexpr (std::is_same_v<T, float>) {
            dds_value.type = vep_VSS_VALUE_TYPE_FLOAT;
            dds_value.float_value = v;
        } else if constexpr (std::is_same_v<T, double>) {
            dds_value.type = vep_VSS_VALUE_TYPE_DOUBLE;
            dds_value.double_value = v;
        } else if constexpr (std::is_same_v<T, std::string>) {
            dds_value.type = vep_VSS_VALUE_TYPE_STRING;
            // Note: string_value will be set by caller with proper lifetime
        } else if constexpr (std::is_same_v<T, std::monostate>) {
            dds_value.type = vep_VSS_VALUE_TYPE_EMPTY;
        }
        // Handle other integer types
        else if constexpr (std::is_integral_v<T>) {
            if constexpr (std::is_signed_v<T>) {
                if constexpr (sizeof(T) <= 4) {
                    dds_value.type = vep_VSS_VALUE_TYPE_INT32;
                    dds_value.int32_value = static_cast<int32_t>(v);
                } else {
                    dds_value.type = vep_VSS_VALUE_TYPE_INT64;
                    dds_value.int64_value = static_cast<int64_t>(v);
                }
            } else {
                if constexpr (sizeof(T) <= 4) {
                    dds_value.type = vep_VSS_VALUE_TYPE_UINT32;
                    dds_value.uint32_value = static_cast<uint32_t>(v);
                } else {
                    dds_value.type = vep_VSS_VALUE_TYPE_UINT64;
                    dds_value.uint64_value = static_cast<uint64_t>(v);
                }
            }
        }
        // Vectors and other types - log and skip for now
        else {
            LOG_FIRST_N(WARNING, 1) << "Unsupported value type in kuksa_to_dds_value";
        }
    }, value);
}

// Get current timestamp in nanoseconds
int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

}  // namespace

KuksaDdsBridge::KuksaDdsBridge(const BridgeConfig& config)
    : config_(config) {
}

KuksaDdsBridge::~KuksaDdsBridge() {
    stop();
}

bool KuksaDdsBridge::initialize() {
    LOG(INFO) << "Initializing Kuksa-DDS Bridge";

    // Create Kuksa resolver (connects to databroker)
    auto resolver_result = kuksa::Resolver::create(config_.kuksa_address);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create Kuksa resolver: " << resolver_result.status();
        return false;
    }
    kuksa_resolver_ = std::move(*resolver_result);

    // Create Kuksa client
    auto client_result = kuksa::Client::create(config_.kuksa_address);
    if (!client_result.ok()) {
        LOG(ERROR) << "Failed to create Kuksa client: " << client_result.status();
        return false;
    }
    kuksa_client_ = std::move(*client_result);

    // Create DDS participant
    try {
        dds_participant_ = std::make_unique<dds::Participant>();
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to create DDS participant: " << e.what();
        return false;
    }

    // Create DDS topics and entities
    try {
        // Signals topic (subscribe - receive from RT/probes)
        dds_signals_topic_ = std::make_unique<dds::Topic>(
            *dds_participant_,
            &vep_VssSignal_desc,
            config_.dds_signals_topic
        );
        dds_signals_reader_ = std::make_unique<dds::Reader>(
            *dds_participant_,
            *dds_signals_topic_
        );

        // Actuator target topic (publish - send to RT bridge)
        dds_actuator_target_topic_ = std::make_unique<dds::Topic>(
            *dds_participant_,
            &vep_VssSignal_desc,
            config_.dds_actuator_target_topic
        );
        dds_actuator_target_writer_ = std::make_unique<dds::Writer>(
            *dds_participant_,
            *dds_actuator_target_topic_
        );

        // Actuator actual topic (subscribe - receive from RT bridge)
        dds_actuator_actual_topic_ = std::make_unique<dds::Topic>(
            *dds_participant_,
            &vep_VssSignal_desc,
            config_.dds_actuator_actual_topic
        );
        dds_actuator_actual_reader_ = std::make_unique<dds::Reader>(
            *dds_participant_,
            *dds_actuator_actual_topic_
        );
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to create DDS entities: " << e.what();
        return false;
    }

    // Resolve all signals from VSS tree
    if (!resolve_all_signals()) {
        LOG(ERROR) << "Failed to resolve signals with Kuksa";
        return false;
    }

    // Register actuators with Kuksa
    if (!register_actuators()) {
        LOG(ERROR) << "Failed to register actuators with Kuksa";
        return false;
    }

    LOG(INFO) << "Kuksa-DDS Bridge initialized successfully";
    return true;
}

bool KuksaDdsBridge::resolve_all_signals() {
    // Query signals from KUKSA databroker's schema
    LOG(INFO) << "Discovering signals from KUKSA broker with pattern: " << config_.signal_pattern;

    auto signals_result = kuksa_resolver_->list_signals(config_.signal_pattern);
    if (!signals_result.ok()) {
        LOG(ERROR) << "Failed to list signals from KUKSA: " << signals_result.status();
        return false;
    }

    auto& handles = *signals_result;
    LOG(INFO) << "Discovered " << handles.size() << " signals from KUKSA broker";

    // Store handles and build actuator path set
    {
        std::lock_guard<std::mutex> lock(handles_mutex_);
        for (const auto& handle : handles) {
            signal_handles_[handle->path()] = handle;
            VLOG(1) << "  Registered handle: " << handle->path();

            // Track actuators for quick lookup
            if (handle->signal_class() == kuksa::SignalClass::ACTUATOR) {
                actuator_paths_.insert(handle->path());
            }
        }
    }

    LOG(INFO) << "Registered " << signal_handles_.size() << " signal handles, "
              << actuator_paths_.size() << " actuators";

    return !handles.empty();
}

bool KuksaDdsBridge::register_actuators() {
    // Get actuators from already-resolved handles
    size_t registered = 0;

    for (const auto& path : actuator_paths_) {
        auto handle = get_signal_handle(path);
        if (!handle) {
            VLOG(1) << "No handle for actuator: " << path;
            continue;
        }

        // Register actuator handler - called when apps set target values
        kuksa_client_->serve_actuator(*handle,
            [this, path](const vss::types::Value& value, const kuksa::DynamicSignalHandle&) {
                // Forward to DDS for RT bridge
                on_actuator_target(path, value);
            }
        );

        registered++;
        VLOG(1) << "Registered actuator: " << path;
    }

    LOG(INFO) << "Registered " << registered << " actuators with Kuksa";
    return true;
}

bool KuksaDdsBridge::start() {
    if (running_) {
        LOG(WARNING) << "Bridge already running";
        return false;
    }

    LOG(INFO) << "Starting Kuksa-DDS Bridge";

    // Start Kuksa client (needed for actuator serving)
    auto start_status = kuksa_client_->start();
    if (!start_status.ok()) {
        LOG(ERROR) << "Failed to start Kuksa client: " << start_status;
        return false;
    }

    // Wait for Kuksa to be ready
    auto ready_status = kuksa_client_->wait_until_ready(std::chrono::milliseconds(10000));
    if (!ready_status.ok()) {
        LOG(ERROR) << "Kuksa client not ready: " << ready_status;
        return false;
    }

    // Start DDS polling thread
    running_ = true;
    dds_poll_thread_ = std::thread(&KuksaDdsBridge::dds_poll_loop, this);

    LOG(INFO) << "Kuksa-DDS Bridge started";
    return true;
}

void KuksaDdsBridge::stop() {
    if (!running_) {
        return;
    }

    LOG(INFO) << "Stopping Kuksa-DDS Bridge";

    running_ = false;

    if (dds_poll_thread_.joinable()) {
        dds_poll_thread_.join();
    }

    if (kuksa_client_) {
        kuksa_client_->stop();
    }

    LOG(INFO) << "Kuksa-DDS Bridge stopped";
}

void KuksaDdsBridge::dds_poll_loop() {
    LOG(INFO) << "DDS poll loop started";

    while (running_) {
        // Poll signals topic (sensors from probes)
        try {
            dds_signals_reader_->take_each<vep_VssSignal>(
                [this](const vep_VssSignal& signal) {
                    on_dds_signal(signal);
                },
                100  // max samples per poll
            );
        } catch (const dds::Error& e) {
            LOG(ERROR) << "Error reading DDS signals: " << e.what();
        }

        // Poll actuator actuals topic (feedback from RT bridge)
        try {
            dds_actuator_actual_reader_->take_each<vep_VssSignal>(
                [this](const vep_VssSignal& signal) {
                    on_dds_actuator_actual(signal);
                },
                100
            );
        } catch (const dds::Error& e) {
            LOG(ERROR) << "Error reading DDS actuator actuals: " << e.what();
        }

        // Small sleep to avoid spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG(INFO) << "DDS poll loop stopped";
}

void KuksaDdsBridge::on_dds_signal(const vep_VssSignal& signal) {
    std::string path = signal.path ? signal.path : "";
    if (path.empty()) {
        LOG(WARNING) << "Received DDS signal with empty path";
        return;
    }

    // Skip actuators - they come via the actuator actual topic
    if (actuator_paths_.count(path) > 0) {
        VLOG(2) << "Skipping actuator on signals topic: " << path;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.dds_signals_received++;
    }

    publish_to_kuksa(path, signal);
}

void KuksaDdsBridge::on_dds_actuator_actual(const vep_VssSignal& signal) {
    std::string path = signal.path ? signal.path : "";
    if (path.empty()) {
        LOG(WARNING) << "Received DDS actuator actual with empty path";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.dds_actuator_actuals_received++;
    }

    // Publish actual value to Kuksa so apps can see the feedback
    publish_to_kuksa(path, signal);
}

void KuksaDdsBridge::publish_to_kuksa(const std::string& path, const vep_VssSignal& signal) {
    // Get cached handle (resolved at init)
    auto handle = get_signal_handle(path);
    if (!handle) {
        LOG_EVERY_N(WARNING, 1000) << "No handle for signal: " << path;
        return;
    }

    // Convert DDS quality to kuksa quality
    vss::types::SignalQuality quality;
    switch (signal.quality) {
        case vep_VSS_QUALITY_VALID:
            quality = vss::types::SignalQuality::VALID;
            break;
        case vep_VSS_QUALITY_NOT_AVAILABLE:
            quality = vss::types::SignalQuality::NOT_AVAILABLE;
            break;
        case vep_VSS_QUALITY_INVALID:
        default:
            quality = vss::types::SignalQuality::INVALID;
            break;
    }

    // Convert value from signal.value (the nested vss_types_Value)
    vss::types::DynamicQualifiedValue qvalue;
    qvalue.value = dds_to_kuksa_value(signal.value);
    qvalue.quality = quality;
    // Convert ns timestamp to time_point
    qvalue.timestamp = std::chrono::system_clock::time_point(
        std::chrono::nanoseconds(signal.header.timestamp_ns));

    // Publish to Kuksa
    auto status = kuksa_client_->publish(*handle, qvalue);
    if (!status.ok()) {
        LOG(WARNING) << "Failed to publish to Kuksa: " << path << " - " << status;
    } else {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.kuksa_signals_published++;
    }
}

std::shared_ptr<kuksa::DynamicSignalHandle> KuksaDdsBridge::get_signal_handle(const std::string& path) {
    // Return cached handle - all handles resolved at initialization
    std::lock_guard<std::mutex> lock(handles_mutex_);
    auto it = signal_handles_.find(path);
    if (it != signal_handles_.end()) {
        return it->second;
    }
    return nullptr;
}

void KuksaDdsBridge::on_actuator_target(const std::string& path, const vss::types::Value& value) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.actuator_requests_received++;
    }

    VLOG(1) << "Actuator target request: " << path;

    // Send to DDS for RT bridge to pick up
    send_actuator_target_to_dds(path, value);
}

void KuksaDdsBridge::send_actuator_target_to_dds(const std::string& path, const vss::types::Value& value) {
    // Prepare DDS message
    vep_VssSignal msg = {};

    // Keep strings alive for write
    std::string source_id = "kuksa_dds_bridge";
    std::string correlation_id = "";

    msg.path = const_cast<char*>(path.c_str());
    msg.header.source_id = const_cast<char*>(source_id.c_str());
    msg.header.timestamp_ns = now_ns();
    msg.header.seq_num = 0;  // Could add sequence tracking
    msg.header.correlation_id = const_cast<char*>(correlation_id.c_str());
    msg.quality = vep_VSS_QUALITY_VALID;

    // Convert value to msg.value (the nested vss_types_Value)
    kuksa_to_dds_value(value, msg.value);

    // Handle string value specially (lifetime)
    std::string string_val;
    if (std::holds_alternative<std::string>(value)) {
        string_val = std::get<std::string>(value);
        msg.value.string_value = const_cast<char*>(string_val.c_str());
    }

    // Write to DDS
    try {
        dds_actuator_target_writer_->write(msg);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.dds_actuator_targets_sent++;
        }
        VLOG(1) << "Sent actuator target to DDS: " << path;
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to write actuator target to DDS: " << e.what();
    }
}

KuksaDdsBridge::Stats KuksaDdsBridge::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace bridge
