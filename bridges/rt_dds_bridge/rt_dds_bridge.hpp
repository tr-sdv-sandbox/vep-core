// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file rt_dds_bridge.hpp
/// @brief RT-DDS Bridge - bridges DDS with Real-Time transport
///
/// Architecture (RT domain - separate from IT/app domain):
///
/// Actuator target flow (from HPC via DDS):
///   DDS rt/vss/actuators/target → Bridge → RtTransport.send() → RT Controller
///
/// Actuator actual flow (from RT Controller):
///   RT Controller → RtTransport callback → Bridge → DDS rt/vss/actuators/actual
///
/// This bridge runs on the RT boundary (e.g., same partition as RT controller,
/// or on a gateway ECU). It translates between DDS messages and the RT transport
/// protocol (AVTP, raw ethernet, shared memory, UDP, etc.).
///
/// The RtTransport interface is abstract - implement for your specific RT protocol.

#include "rt_transport.hpp"

#include "common/dds_wrapper.hpp"
#include "vss-signal.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <functional>

namespace rt_bridge {

/// Configuration for the RT-DDS Bridge
struct RtBridgeConfig {
    // DDS topic names
    std::string dds_actuator_target_topic = "rt/vss/actuators/target";
    std::string dds_actuator_actual_topic = "rt/vss/actuators/actual";

    // RT transport type (for factory)
    std::string rt_transport_type = "logging";  // "logging", "loopback", "udp", "avtp", etc.

    // Loopback transport config (if rt_transport_type == "loopback")
    int loopback_delay_ms = 100;  // Delay before echoing actual

    // UDP transport config (if rt_transport_type == "udp")
    std::string udp_target_host = "127.0.0.1";
    uint16_t udp_target_port = 9000;
    uint16_t udp_listen_port = 9001;
    std::string udp_multicast_interface = "";  // e.g., "eth0" for multicast
};

/// RT-DDS Bridge
///
/// Bridges actuator commands between DDS and RT transport.
/// - Subscribes to DDS actuator targets, forwards to RT
/// - Receives actuals from RT, publishes to DDS
class RtDdsBridge {
public:
    explicit RtDdsBridge(const RtBridgeConfig& config);
    ~RtDdsBridge();

    /// Set the RT transport implementation
    /// Must be called before initialize()
    void set_rt_transport(std::shared_ptr<bridge::RtTransport> transport);

    /// Initialize the bridge
    /// - Creates DDS entities
    /// - Initializes RT transport
    /// @return true on success
    bool initialize();

    /// Start the bridge
    /// - Starts DDS subscription
    /// - Registers RT callback
    /// @return true on success
    bool start();

    /// Stop the bridge
    void stop();

    /// Get statistics
    struct Stats {
        uint64_t dds_targets_received = 0;
        uint64_t rt_commands_sent = 0;
        uint64_t rt_actuals_received = 0;
        uint64_t dds_actuals_published = 0;
    };
    Stats stats() const;

private:
    // DDS target handler - called when actuator targets arrive from DDS
    void on_dds_actuator_target(const vep_VssSignal& signal);

    // RT actual handler - called when RT reports actual values
    void on_rt_actual(const std::string& path, const bridge::ActuatorValue& value);

    // Publish actual to DDS
    void publish_actual_to_dds(const std::string& path, const bridge::ActuatorValue& value);

    // Convert DDS signal to ActuatorValue
    bridge::ActuatorValue dds_to_actuator_value(const vep_VssSignal& signal);

    RtBridgeConfig config_;

    // DDS entities
    std::unique_ptr<dds::Participant> dds_participant_;
    std::unique_ptr<dds::Topic> dds_target_topic_;
    std::unique_ptr<dds::Reader> dds_target_reader_;
    std::unique_ptr<dds::Topic> dds_actual_topic_;
    std::unique_ptr<dds::Writer> dds_actual_writer_;

    // RT transport
    std::shared_ptr<bridge::RtTransport> rt_transport_;

    // DDS polling thread
    std::thread dds_poll_thread_;
    std::atomic<bool> running_{false};

    // Stats
    mutable std::mutex stats_mutex_;
    Stats stats_;

    void dds_poll_loop();
};

/// Factory function to create RT transport by type
std::shared_ptr<bridge::RtTransport> create_rt_transport(
    const std::string& type,
    const RtBridgeConfig& config
);

}  // namespace rt_bridge
