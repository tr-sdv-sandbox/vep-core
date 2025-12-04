// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Kuksa-DDS Bridge - bridges Kuksa databroker with DDS for the VDR ecosystem
///
/// This bridge enables:
/// - Sensors: DDS rt/vss/signals → Kuksa (apps can subscribe via Kuksa)
/// - Actuators: Kuksa set() → DDS rt/vss/actuators/target (RT bridge picks up)
/// - Actuals: DDS rt/vss/actuators/actual → Kuksa (apps see feedback)
///
/// Usage:
///   kuksa_dds_bridge --kuksa=localhost:55555
///   kuksa_dds_bridge --kuksa=localhost:55555 --pattern=Vehicle.Cabin
///
/// The bridge queries KUKSA's schema to discover signals - no local VSS JSON needed.
/// The bridge does NOT communicate directly with RT - that's handled by
/// a separate rt_dds_bridge component to maintain clean domain separation.

#include "kuksa_dds_bridge.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

// Command line flags
DEFINE_string(kuksa, "localhost:55555", "Kuksa databroker address");
DEFINE_string(pattern, "Vehicle", "Signal discovery pattern (e.g., Vehicle, Vehicle.Cabin)");
DEFINE_string(signals_topic, "rt/vss/signals", "DDS topic for sensor signals");
DEFINE_string(actuator_target_topic, "rt/vss/actuators/target", "DDS topic for actuator targets");
DEFINE_string(actuator_actual_topic, "rt/vss/actuators/actual", "DDS topic for actuator actuals");
DEFINE_int32(stats_interval, 30, "Statistics logging interval in seconds (0=disabled)");

// Global shutdown flag
std::atomic<bool> g_shutdown{false};

void signal_handler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_shutdown = true;
}

int main(int argc, char* argv[]) {
    // Initialize logging and flags
    google::InitGoogleLogging(argv[0]);
    gflags::SetUsageMessage("Kuksa-DDS Bridge - bridges Kuksa databroker with DDS");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    FLAGS_logtostderr = true;

    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG(INFO) << "Starting Kuksa-DDS Bridge";
    LOG(INFO) << "  Kuksa address: " << FLAGS_kuksa;
    LOG(INFO) << "  Signal pattern: " << FLAGS_pattern;
    LOG(INFO) << "  Signals topic: " << FLAGS_signals_topic;
    LOG(INFO) << "  Actuator target topic: " << FLAGS_actuator_target_topic;
    LOG(INFO) << "  Actuator actual topic: " << FLAGS_actuator_actual_topic;

    // Configure bridge
    bridge::BridgeConfig config;
    config.kuksa_address = FLAGS_kuksa;
    config.signal_pattern = FLAGS_pattern;
    config.dds_signals_topic = FLAGS_signals_topic;
    config.dds_actuator_target_topic = FLAGS_actuator_target_topic;
    config.dds_actuator_actual_topic = FLAGS_actuator_actual_topic;

    // Create and initialize bridge
    bridge::KuksaDdsBridge bridge(config);

    if (!bridge.initialize()) {
        LOG(ERROR) << "Failed to initialize bridge";
        return 1;
    }

    if (!bridge.start()) {
        LOG(ERROR) << "Failed to start bridge";
        return 1;
    }

    LOG(INFO) << "Bridge running. Press Ctrl+C to stop.";

    // Main loop - just wait for shutdown signal
    auto last_stats_time = std::chrono::steady_clock::now();

    while (!g_shutdown) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Periodic stats logging
        if (FLAGS_stats_interval > 0) {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count();
            if (elapsed >= FLAGS_stats_interval) {
                auto stats = bridge.stats();
                LOG(INFO) << "Bridge stats:"
                          << " dds_signals=" << stats.dds_signals_received
                          << " dds_actuals=" << stats.dds_actuator_actuals_received
                          << " kuksa_published=" << stats.kuksa_signals_published
                          << " actuator_requests=" << stats.actuator_requests_received
                          << " dds_targets_sent=" << stats.dds_actuator_targets_sent;
                last_stats_time = now;
            }
        }
    }

    LOG(INFO) << "Shutting down bridge...";
    bridge.stop();

    // Final stats
    auto stats = bridge.stats();
    LOG(INFO) << "Final stats:"
              << " dds_signals=" << stats.dds_signals_received
              << " dds_actuals=" << stats.dds_actuator_actuals_received
              << " kuksa_published=" << stats.kuksa_signals_published
              << " actuator_requests=" << stats.actuator_requests_received
              << " dds_targets_sent=" << stats.dds_actuator_targets_sent;

    LOG(INFO) << "Bridge stopped";
    return 0;
}
