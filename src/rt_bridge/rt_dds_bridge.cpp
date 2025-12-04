// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rt_dds_bridge.hpp"

#include <glog/logging.h>
#include <chrono>

namespace rt_bridge {

namespace {

// Get current timestamp in nanoseconds
int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

// Convert ActuatorValue to DDS signal value type and set fields
void actuator_value_to_dds(const bridge::ActuatorValue& value, vss_Signal& signal) {
    std::visit([&signal](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            signal.value.type = vss_types_VALUE_TYPE_BOOL;
            signal.value.bool_value = v;
        } else if constexpr (std::is_same_v<T, int8_t>) {
            signal.value.type = vss_types_VALUE_TYPE_INT8;
            signal.value.int8_value = static_cast<uint8_t>(v);
        } else if constexpr (std::is_same_v<T, int16_t>) {
            signal.value.type = vss_types_VALUE_TYPE_INT16;
            signal.value.int16_value = v;
        } else if constexpr (std::is_same_v<T, int32_t>) {
            signal.value.type = vss_types_VALUE_TYPE_INT32;
            signal.value.int32_value = v;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            signal.value.type = vss_types_VALUE_TYPE_INT64;
            signal.value.int64_value = v;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            signal.value.type = vss_types_VALUE_TYPE_UINT8;
            signal.value.uint8_value = v;
        } else if constexpr (std::is_same_v<T, uint16_t>) {
            signal.value.type = vss_types_VALUE_TYPE_UINT16;
            signal.value.uint16_value = v;
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            signal.value.type = vss_types_VALUE_TYPE_UINT32;
            signal.value.uint32_value = v;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            signal.value.type = vss_types_VALUE_TYPE_UINT64;
            signal.value.uint64_value = v;
        } else if constexpr (std::is_same_v<T, float>) {
            signal.value.type = vss_types_VALUE_TYPE_FLOAT;
            signal.value.float_value = v;
        } else if constexpr (std::is_same_v<T, double>) {
            signal.value.type = vss_types_VALUE_TYPE_DOUBLE;
            signal.value.double_value = v;
        } else if constexpr (std::is_same_v<T, std::string>) {
            signal.value.type = vss_types_VALUE_TYPE_STRING;
            // Note: string_value must be set by caller with proper lifetime
        }
    }, value);
}

}  // namespace

RtDdsBridge::RtDdsBridge(const RtBridgeConfig& config)
    : config_(config) {
}

RtDdsBridge::~RtDdsBridge() {
    stop();
}

void RtDdsBridge::set_rt_transport(std::shared_ptr<bridge::RtTransport> transport) {
    rt_transport_ = std::move(transport);
}

bool RtDdsBridge::initialize() {
    LOG(INFO) << "Initializing RT-DDS Bridge";

    // Create RT transport if not set
    if (!rt_transport_) {
        rt_transport_ = create_rt_transport(config_.rt_transport_type, config_);
        if (!rt_transport_) {
            LOG(ERROR) << "Failed to create RT transport: " << config_.rt_transport_type;
            return false;
        }
    }

    // Initialize RT transport
    if (!rt_transport_->initialize()) {
        LOG(ERROR) << "Failed to initialize RT transport";
        return false;
    }

    // Create DDS participant
    try {
        dds_participant_ = std::make_unique<dds::Participant>();
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to create DDS participant: " << e.what();
        return false;
    }

    // Create DDS topics and entities
    try {
        // Actuator target topic (subscribe - receive from kuksa_dds_bridge)
        dds_target_topic_ = std::make_unique<dds::Topic>(
            *dds_participant_,
            &vss_Signal_desc,
            config_.dds_actuator_target_topic
        );
        dds_target_reader_ = std::make_unique<dds::Reader>(
            *dds_participant_,
            *dds_target_topic_
        );

        // Actuator actual topic (publish - send to kuksa_dds_bridge)
        dds_actual_topic_ = std::make_unique<dds::Topic>(
            *dds_participant_,
            &vss_Signal_desc,
            config_.dds_actuator_actual_topic
        );
        dds_actual_writer_ = std::make_unique<dds::Writer>(
            *dds_participant_,
            *dds_actual_topic_
        );
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to create DDS entities: " << e.what();
        return false;
    }

    LOG(INFO) << "RT-DDS Bridge initialized successfully";
    LOG(INFO) << "  Target topic: " << config_.dds_actuator_target_topic;
    LOG(INFO) << "  Actual topic: " << config_.dds_actuator_actual_topic;
    LOG(INFO) << "  RT transport: " << config_.rt_transport_type;

    return true;
}

bool RtDdsBridge::start() {
    if (running_) {
        LOG(WARNING) << "Bridge already running";
        return false;
    }

    LOG(INFO) << "Starting RT-DDS Bridge";

    // Register RT callback for actual values
    rt_transport_->on_actual_value(
        [this](const std::string& path, const bridge::ActuatorValue& value) {
            on_rt_actual(path, value);
        }
    );

    // Start DDS polling thread
    running_ = true;
    dds_poll_thread_ = std::thread(&RtDdsBridge::dds_poll_loop, this);

    LOG(INFO) << "RT-DDS Bridge started";
    return true;
}

void RtDdsBridge::stop() {
    if (!running_) {
        return;
    }

    LOG(INFO) << "Stopping RT-DDS Bridge";

    running_ = false;

    if (dds_poll_thread_.joinable()) {
        dds_poll_thread_.join();
    }

    if (rt_transport_) {
        rt_transport_->shutdown();
    }

    LOG(INFO) << "RT-DDS Bridge stopped";
}

void RtDdsBridge::dds_poll_loop() {
    LOG(INFO) << "DDS poll loop started";

    while (running_) {
        // Poll actuator targets from DDS
        try {
            dds_target_reader_->take_each<vss_Signal>(
                [this](const vss_Signal& signal) {
                    on_dds_actuator_target(signal);
                },
                100  // max samples per poll
            );
        } catch (const dds::Error& e) {
            LOG(ERROR) << "Error reading DDS targets: " << e.what();
        }

        // Small sleep to avoid spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    LOG(INFO) << "DDS poll loop stopped";
}

void RtDdsBridge::on_dds_actuator_target(const vss_Signal& signal) {
    std::string path = signal.path ? signal.path : "";
    if (path.empty()) {
        LOG(WARNING) << "Received DDS actuator target with empty path";
        return;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.dds_targets_received++;
    }

    // Convert to ActuatorValue
    bridge::ActuatorValue value = dds_to_actuator_value(signal);

    // Send to RT transport
    if (rt_transport_->send_actuator_target(path, value)) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.rt_commands_sent++;
    } else {
        LOG(WARNING) << "Failed to send actuator target to RT: " << path;
    }
}

bridge::ActuatorValue RtDdsBridge::dds_to_actuator_value(const vss_Signal& signal) {
    switch (signal.value.type) {
        case vss_types_VALUE_TYPE_BOOL:
            return bridge::ActuatorValue{signal.value.bool_value};
        case vss_types_VALUE_TYPE_INT8:
            return bridge::ActuatorValue{static_cast<int8_t>(signal.value.int8_value)};
        case vss_types_VALUE_TYPE_INT16:
            return bridge::ActuatorValue{signal.value.int16_value};
        case vss_types_VALUE_TYPE_INT32:
            return bridge::ActuatorValue{signal.value.int32_value};
        case vss_types_VALUE_TYPE_INT64:
            return bridge::ActuatorValue{signal.value.int64_value};
        case vss_types_VALUE_TYPE_UINT8:
            return bridge::ActuatorValue{signal.value.uint8_value};
        case vss_types_VALUE_TYPE_UINT16:
            return bridge::ActuatorValue{signal.value.uint16_value};
        case vss_types_VALUE_TYPE_UINT32:
            return bridge::ActuatorValue{signal.value.uint32_value};
        case vss_types_VALUE_TYPE_UINT64:
            return bridge::ActuatorValue{signal.value.uint64_value};
        case vss_types_VALUE_TYPE_FLOAT:
            return bridge::ActuatorValue{signal.value.float_value};
        case vss_types_VALUE_TYPE_DOUBLE:
            return bridge::ActuatorValue{signal.value.double_value};
        case vss_types_VALUE_TYPE_STRING:
            return bridge::ActuatorValue{std::string(signal.value.string_value ? signal.value.string_value : "")};
        default:
            LOG(WARNING) << "Unknown value type: " << signal.value.type;
            return bridge::ActuatorValue{false};
    }
}

void RtDdsBridge::on_rt_actual(const std::string& path, const bridge::ActuatorValue& value) {
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.rt_actuals_received++;
    }

    VLOG(1) << "RT actual received: " << path;

    publish_actual_to_dds(path, value);
}

void RtDdsBridge::publish_actual_to_dds(const std::string& path, const bridge::ActuatorValue& value) {
    // Prepare DDS message
    vss_Signal msg = {};

    // Keep strings alive for write
    std::string source_id = "rt_dds_bridge";
    std::string correlation_id = "";

    msg.path = const_cast<char*>(path.c_str());
    msg.header.source_id = const_cast<char*>(source_id.c_str());
    msg.header.timestamp_ns = now_ns();
    msg.header.seq_num = 0;
    msg.header.correlation_id = const_cast<char*>(correlation_id.c_str());
    msg.quality = vss_types_QUALITY_VALID;

    // Convert value
    actuator_value_to_dds(value, msg);

    // Handle string value specially (lifetime)
    std::string string_val;
    if (std::holds_alternative<std::string>(value)) {
        string_val = std::get<std::string>(value);
        msg.value.string_value = const_cast<char*>(string_val.c_str());
    }

    // Write to DDS
    try {
        dds_actual_writer_->write(msg);
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.dds_actuals_published++;
        }
        VLOG(1) << "Published actuator actual to DDS: " << path;
    } catch (const dds::Error& e) {
        LOG(ERROR) << "Failed to write actuator actual to DDS: " << e.what();
    }
}

RtDdsBridge::Stats RtDdsBridge::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// Factory function
std::shared_ptr<bridge::RtTransport> create_rt_transport(
    const std::string& type,
    const RtBridgeConfig& config
) {
    if (type == "logging") {
        return std::make_shared<bridge::LoggingRtTransport>();
    }
    else if (type == "loopback") {
        return std::make_shared<bridge::LoopbackRtTransport>(config.loopback_delay_ms);
    }
    // Future: add more transport types
    // else if (type == "udp") {
    //     return std::make_shared<UdpRtTransport>(config.udp_target_host, config.udp_target_port, config.udp_listen_port);
    // }
    // else if (type == "avtp") {
    //     return std::make_shared<AvtpRtTransport>(...);
    // }

    LOG(ERROR) << "Unknown RT transport type: " << type;
    return nullptr;
}

}  // namespace rt_bridge
