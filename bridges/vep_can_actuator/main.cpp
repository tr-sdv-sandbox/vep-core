// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// vep_can_actuator - DDS to CAN actuator bridge
//
// Subscribes to DDS actuator topic (rt/vss/actuators/target), encodes
// actuator values to CAN frames using vss_dbc.json mapping, and writes
// to SocketCAN interface.
//
// Usage:
//   vep_can_actuator --config /path/to/vss_dbc.json --interface vcan0

#include "can_encoder.hpp"
#include "can_writer.hpp"
#include "vss_dbc_parser.hpp"

#include "common/dds_wrapper.hpp"
#include "common/qos_profiles.hpp"
#include "vss-signal.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>
#include <unordered_map>

DEFINE_string(config, "", "Path to vss_dbc.json configuration file");
DEFINE_string(interface, "vcan0", "CAN interface name (e.g., vcan0, can0)");
DEFINE_string(topic, "rt/vss/actuators/target", "DDS topic to subscribe to");

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int signum) {
    LOG(INFO) << "Received signal " << signum << ", shutting down...";
    g_running = false;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Initialize logging
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    FLAGS_colorlogtostderr = true;

    // Parse command line flags
    gflags::SetUsageMessage(
        "DDS to CAN actuator bridge\n\n"
        "Subscribes to DDS actuator topic and writes encoded values to CAN.\n\n"
        "Example:\n"
        "  vep_can_actuator --config vss_dbc.json --interface vcan0");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_config.empty()) {
        LOG(ERROR) << "Missing required flag: --config";
        LOG(ERROR) << "Usage: vep_can_actuator --config /path/to/vss_dbc.json --interface vcan0";
        return 1;
    }

    LOG(INFO) << "vep_can_actuator starting...";
    LOG(INFO) << "  Config: " << FLAGS_config;
    LOG(INFO) << "  Interface: " << FLAGS_interface;
    LOG(INFO) << "  DDS Topic: " << FLAGS_topic;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        // Load vss_dbc.json configuration
        vep::VssDbcParser parser;
        if (!parser.load(FLAGS_config)) {
            LOG(ERROR) << "Failed to load configuration: " << FLAGS_config;
            return 1;
        }

        LOG(INFO) << "Loaded " << parser.mapping_count() << " actuator mappings";
        for (const auto& path : parser.get_actuator_paths()) {
            LOG(INFO) << "  - " << path;
        }

        // Open CAN writer
        vep::SocketCANWriter writer;
        if (!writer.open(FLAGS_interface)) {
            LOG(ERROR) << "Failed to open CAN interface: " << FLAGS_interface;
            return 1;
        }

        // Create CAN encoder
        vep::CANEncoder encoder;

        // Create DDS participant and reader
        dds::Participant participant(DDS_DOMAIN_DEFAULT);

        auto qos = dds::qos_profiles::reliable_standard(100);
        dds::Topic topic(participant, &vep_VssSignal_desc, FLAGS_topic, qos.get());
        dds::Reader reader(participant, topic, qos.get());

        LOG(INFO) << "DDS reader created for topic: " << FLAGS_topic;
        LOG(INFO) << "vep_can_actuator ready. Press Ctrl+C to stop.";

        // Track frame buffers per CAN ID to handle multiple signals in same message
        std::unordered_map<uint32_t, std::vector<uint8_t>> frame_buffers;

        uint64_t signals_received = 0;
        uint64_t signals_encoded = 0;

        while (g_running) {
            // Poll for DDS messages with timeout
            if (reader.wait(100)) {  // 100ms timeout
                reader.take_each<vep_VssSignal>([&](const vep_VssSignal& msg) {
                    signals_received++;

                    // Only process valid signals
                    if (msg.quality != vep_VSS_QUALITY_VALID) {
                        VLOG(1) << "Skipping invalid signal: " << (msg.path ? msg.path : "null");
                        return;
                    }

                    if (msg.path == nullptr) {
                        LOG(WARNING) << "Received signal with null path";
                        return;
                    }

                    std::string path = msg.path;

                    // Find mapping for this actuator
                    const vep::CANSignalMapping* mapping = parser.find_mapping(path);
                    if (mapping == nullptr) {
                        VLOG(2) << "No mapping found for: " << path;
                        return;
                    }

                    // Get or create frame buffer for this CAN ID
                    auto& frame_data = frame_buffers[mapping->can_id];
                    if (frame_data.size() < mapping->message_length) {
                        frame_data.resize(mapping->message_length, 0);
                    }

                    // Encode signal into frame buffer
                    if (encoder.encode_signal(*mapping, msg.value, frame_data)) {
                        // Write CAN frame
                        if (writer.write(mapping->can_id, frame_data)) {
                            signals_encoded++;
                            VLOG(1) << "Sent CAN frame for " << path
                                    << " -> CAN ID 0x" << std::hex << mapping->can_id;
                        }
                    } else {
                        LOG(WARNING) << "Failed to encode signal: " << path;
                    }
                }, 100);
            }

            LOG_EVERY_N(INFO, 100) << "Stats: received=" << signals_received
                                   << " encoded=" << signals_encoded
                                   << " sent=" << writer.frames_sent()
                                   << " errors=" << writer.send_errors();
        }

        // Cleanup
        writer.close();

        LOG(INFO) << "vep_can_actuator shutdown complete.";
        LOG(INFO) << "Final stats: received=" << signals_received
                  << " encoded=" << signals_encoded
                  << " sent=" << writer.frames_sent()
                  << " errors=" << writer.send_errors();

    } catch (const dds::Error& e) {
        LOG(FATAL) << "DDS error: " << e.what();
        return 1;
    } catch (const std::exception& e) {
        LOG(FATAL) << "Error: " << e.what();
        return 1;
    }

    gflags::ShutDownCommandLineFlags();
    google::ShutdownGoogleLogging();
    return 0;
}
