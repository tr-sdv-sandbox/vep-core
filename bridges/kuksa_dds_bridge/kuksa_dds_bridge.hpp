// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file kuksa_dds_bridge.hpp
/// @brief Kuksa-DDS Bridge - bidirectional bridge between Kuksa databroker and DDS
///
/// Architecture (IT/App domain only - does NOT reach RT directly):
///
/// Sensor flow (DDS → Kuksa):
///   DDS rt/vss/signals → Bridge → Kuksa.publish() → Apps
///
/// Actuator target flow (Apps → DDS):
///   Apps → Kuksa.set() → Bridge.serve_actuator() → DDS rt/vss/actuators/target
///
/// Actuator actual flow (DDS → Kuksa):
///   DDS rt/vss/actuators/actual → Bridge → Kuksa.publish() → Apps subscription
///
/// NOTE: A separate rt_dds_bridge component handles DDS ↔ RT transport.
/// This keeps IT/app domain cleanly separated from RT domain.
///
/// The bridge queries the KUKSA databroker's schema to discover available signals,
/// rather than loading a local VSS JSON file.

#include <kuksa_cpp/kuksa.hpp>
#include "common/dds_wrapper.hpp"
#include "vss-signal.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

namespace bridge {

/// Configuration for the Kuksa-DDS Bridge
struct BridgeConfig {
    std::string kuksa_address = "localhost:55555";

    // Signal discovery pattern (passed to KUKSA ListMetadata)
    // Default "Vehicle" lists all signals under Vehicle branch
    std::string signal_pattern = "Vehicle";

    // DDS topic names
    std::string dds_signals_topic = "rt/vss/signals";
    std::string dds_actuator_target_topic = "rt/vss/actuators/target";
    std::string dds_actuator_actual_topic = "rt/vss/actuators/actual";

    // Timeout waiting for KUKSA client to be ready (seconds)
    // Increase for many actuators or slow targets (ARM64)
    int ready_timeout_seconds = 60;
};

/// Kuksa-DDS Bridge
///
/// Bridges signals between Kuksa databroker and DDS in both directions.
/// Queries KUKSA's schema to determine signal types (sensor vs actuator).
///
/// Does NOT handle RT transport directly - that's done by a separate component.
class KuksaDdsBridge {
public:
    explicit KuksaDdsBridge(const BridgeConfig& config);
    ~KuksaDdsBridge();

    /// Initialize the bridge
    /// - Connects to Kuksa and discovers signals from schema
    /// - Creates DDS entities
    /// - Registers actuators with Kuksa
    /// @return true on success
    bool initialize();

    /// Start the bridge
    /// - Starts DDS subscriptions (signals + actuator actuals)
    /// - Starts Kuksa client
    /// @return true on success
    bool start();

    /// Stop the bridge
    void stop();

    /// Get statistics
    struct Stats {
        uint64_t dds_signals_received = 0;
        uint64_t dds_actuator_actuals_received = 0;
        uint64_t kuksa_signals_published = 0;
        uint64_t actuator_requests_received = 0;
        uint64_t dds_actuator_targets_sent = 0;
    };
    Stats stats() const;

private:
    // DDS signal handler - called when sensor signals arrive from DDS
    void on_dds_signal(const vep_VssSignal& signal);

    // DDS actuator actual handler - called when actuator actuals arrive from DDS
    void on_dds_actuator_actual(const vep_VssSignal& signal);

    // Actuator target handler - called when Kuksa receives actuator set request from apps
    void on_actuator_target(const std::string& path, const vss::types::Value& value);

    // Publish signal to Kuksa (from DDS)
    void publish_to_kuksa(const std::string& path, const vep_VssSignal& signal);

    // Send actuator target to DDS (for RT bridge to pick up)
    void send_actuator_target_to_dds(const std::string& path, const vss::types::Value& value);

    // Register all actuators with Kuksa
    bool register_actuators();

    // Resolve all signals from VSS tree at initialization
    bool resolve_all_signals();

    // Get cached handle for a path (returns nullptr if not resolved)
    std::shared_ptr<kuksa::DynamicSignalHandle> get_signal_handle(const std::string& path);

    BridgeConfig config_;

    // Kuksa client
    std::unique_ptr<kuksa::Client> kuksa_client_;
    std::unique_ptr<kuksa::Resolver> kuksa_resolver_;

    // DDS entities for signals (sensors from RT)
    std::unique_ptr<dds::Participant> dds_participant_;
    std::unique_ptr<dds::Topic> dds_signals_topic_;
    std::unique_ptr<dds::Reader> dds_signals_reader_;

    // DDS entities for actuator targets (to RT)
    std::unique_ptr<dds::Topic> dds_actuator_target_topic_;
    std::unique_ptr<dds::Writer> dds_actuator_target_writer_;

    // DDS entities for actuator actuals (from RT)
    std::unique_ptr<dds::Topic> dds_actuator_actual_topic_;
    std::unique_ptr<dds::Reader> dds_actuator_actual_reader_;

    // Signal handles cache (path -> handle)
    // Resolved at initialization from VSS tree - no lazy resolution
    std::unordered_map<std::string, std::shared_ptr<kuksa::DynamicSignalHandle>> signal_handles_;
    std::mutex handles_mutex_;

    // Set of actuator paths (for quick lookup)
    std::unordered_set<std::string> actuator_paths_;

    // DDS polling thread
    std::thread dds_poll_thread_;
    std::atomic<bool> running_{false};

    // Stats
    mutable std::mutex stats_mutex_;
    Stats stats_;

    void dds_poll_loop();
};

}  // namespace bridge
