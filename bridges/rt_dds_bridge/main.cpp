// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief RT-DDS Bridge - bridges DDS with Real-Time transport
///
/// This bridge handles the RT boundary:
/// - Receives actuator targets from DDS (sent by kuksa_dds_bridge)
/// - Forwards them to RT controller via RT transport
/// - Receives actuals from RT controller
/// - Publishes actuals to DDS (picked up by kuksa_dds_bridge)
///
/// Usage:
///   rt_dds_bridge --transport=logging
///   rt_dds_bridge --transport=udp --udp-host=192.168.1.100 --udp-port=9000
///
/// The transport type determines how we communicate with the RT controller:
/// - logging: Just logs commands (for testing)
/// - udp: Send/receive via UDP (future)
/// - avtp: IEEE 1722 AVTP (future)
/// - shm: Shared memory (future)

#include "rt_dds_bridge.hpp"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>

// Command line flags
DEFINE_string(transport, "logging", "RT transport type: logging, loopback, udp, avtp, shm");
DEFINE_string(target_topic, "rt/vss/actuators/target", "DDS topic for actuator targets");
DEFINE_string(actual_topic, "rt/vss/actuators/actual", "DDS topic for actuator actuals");
DEFINE_int32(loopback_delay_ms, 100, "Delay before loopback echoes actual (ms)");
DEFINE_string(udp_host, "127.0.0.1", "UDP target host - unicast or multicast (for udp transport)");
DEFINE_int32(udp_port, 9000, "UDP target port (for udp transport)");
DEFINE_int32(udp_listen_port, 9001, "UDP listen port for actuals (for udp transport)");
DEFINE_string(udp_interface, "", "Network interface for multicast (e.g., eth0, empty=default)");
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
    gflags::SetUsageMessage("RT-DDS Bridge - bridges DDS with Real-Time transport");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    FLAGS_logtostderr = true;

    // Install signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG(INFO) << "Starting RT-DDS Bridge";
    LOG(INFO) << "  Transport: " << FLAGS_transport;
    LOG(INFO) << "  Target topic: " << FLAGS_target_topic;
    LOG(INFO) << "  Actual topic: " << FLAGS_actual_topic;
    if (FLAGS_transport == "loopback") {
        LOG(INFO) << "  Loopback delay: " << FLAGS_loopback_delay_ms << "ms";
    }
    if (FLAGS_transport == "udp") {
        LOG(INFO) << "  UDP host: " << FLAGS_udp_host << ":" << FLAGS_udp_port;
        LOG(INFO) << "  UDP listen: " << FLAGS_udp_listen_port;
        if (!FLAGS_udp_interface.empty()) {
            LOG(INFO) << "  UDP interface: " << FLAGS_udp_interface;
        }
    }

    // Configure bridge
    rt_bridge::RtBridgeConfig config;
    config.dds_actuator_target_topic = FLAGS_target_topic;
    config.dds_actuator_actual_topic = FLAGS_actual_topic;
    config.rt_transport_type = FLAGS_transport;
    config.loopback_delay_ms = FLAGS_loopback_delay_ms;
    config.udp_target_host = FLAGS_udp_host;
    config.udp_target_port = static_cast<uint16_t>(FLAGS_udp_port);
    config.udp_listen_port = static_cast<uint16_t>(FLAGS_udp_listen_port);
    config.udp_multicast_interface = FLAGS_udp_interface;

    // Create bridge
    rt_bridge::RtDdsBridge bridge(config);

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
                          << " dds_targets=" << stats.dds_targets_received
                          << " rt_commands=" << stats.rt_commands_sent
                          << " rt_actuals=" << stats.rt_actuals_received
                          << " dds_actuals=" << stats.dds_actuals_published;
                last_stats_time = now;
            }
        }
    }

    LOG(INFO) << "Shutting down bridge...";
    bridge.stop();

    // Final stats
    auto stats = bridge.stats();
    LOG(INFO) << "Final stats:"
              << " dds_targets=" << stats.dds_targets_received
              << " rt_commands=" << stats.rt_commands_sent
              << " rt_actuals=" << stats.rt_actuals_received
              << " dds_actuals=" << stats.dds_actuals_published;

    LOG(INFO) << "Bridge stopped";
    return 0;
}
