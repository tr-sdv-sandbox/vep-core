// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief VEP Exporter IFEX - Exports telemetry via IFEX BackendTransport (gRPC)
///
/// This application wires SubscriptionManager with UnifiedExporterPipeline
/// to create a bandwidth-efficient vehicle-to-cloud data export pipeline.
///
/// Architecture:
///   DDS Topics → SubscriptionManager → UnifiedExporterPipeline → IfexBackendTransport
///                                                                      ↓
///                                                             gRPC BackendTransport
///                                                                      ↓
///                                                           grpc_backend_proxy (SOME/IP)
///                                                                   or
///                                                           backend-transport-server (MQTT)
///
/// Usage:
///   vep_exporter_ifex [options]

#include "common/dds_wrapper.hpp"
#include "unified_pipeline.hpp"
#include "ifex_backend_transport.hpp"
#include "compressor.hpp"
#include "subscriber.hpp"

#include <glog/logging.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    LOG(INFO) << "Received signal " << sig << ", shutting down...";
    g_running = false;
}

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "VEP Exporter IFEX - Exports vehicle telemetry via IFEX gRPC\n"
              << "\n"
              << "Options:\n"
              << "  --grpc-target HOST:PORT  IFEX BackendTransport gRPC endpoint\n"
              << "                           (default: localhost:50060)\n"
              << "  --content-id ID          Content ID for transport routing (default: 1)\n"
              << "  --batch-size N           Max items per batch (default: 100)\n"
              << "  --batch-timeout MS       Batch timeout in ms (default: 1000)\n"
              << "  --compression N          Zstd compression level 1-19 (default: 3)\n"
              << "  --no-compression         Disable compression\n"
              << "  --help                   Show this help message\n"
              << "\n"
              << "Environment:\n"
              << "  GRPC_TARGET              Override --grpc-target\n"
              << "  CONTENT_ID               Override --content-id\n"
              << "\n"
              << "Example:\n"
              << "  " << prog << " --grpc-target localhost:50060 --content-id 1\n"
              << "\n";
}

struct Config {
    vep::IfexBackendTransportConfig transport;
    vep::exporter::UnifiedPipelineConfig pipeline;
    integration::SubscriptionConfig sub;
    std::string compressor_type = "zstd";
    int compression_level = 3;
};

Config parse_args(int argc, char* argv[]) {
    Config config;

    // Environment variable defaults
    if (const char* env = std::getenv("GRPC_TARGET")) {
        config.transport.grpc_target = env;
    }
    if (const char* env = std::getenv("CONTENT_ID")) {
        config.transport.content_id = static_cast<uint32_t>(std::stoul(env));
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--grpc-target" && i + 1 < argc) {
            config.transport.grpc_target = argv[++i];
        } else if (arg == "--content-id" && i + 1 < argc) {
            config.transport.content_id = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--batch-size" && i + 1 < argc) {
            config.pipeline.batch_max_items = std::stoul(argv[++i]);
        } else if (arg == "--batch-timeout" && i + 1 < argc) {
            config.pipeline.batch_timeout = std::chrono::milliseconds(std::stoi(argv[++i]));
        } else if (arg == "--compression" && i + 1 < argc) {
            config.compression_level = std::stoi(argv[++i]);
        } else if (arg == "--no-compression") {
            config.compressor_type = "none";
        } else {
            LOG(WARNING) << "Unknown argument: " << arg;
        }
    }

    return config;
}

void log_config(const Config& config) {
    LOG(INFO) << "=== VEP Exporter IFEX Configuration ===";
    LOG(INFO) << "gRPC Target: " << config.transport.grpc_target;
    LOG(INFO) << "Content ID: " << config.transport.content_id;
    LOG(INFO) << "Batching: " << config.pipeline.batch_max_items << " items, "
              << config.pipeline.batch_timeout.count() << "ms timeout";
    LOG(INFO) << "Compression: " << config.compressor_type
              << (config.compressor_type == "zstd" ? " (level " + std::to_string(config.compression_level) + ")" : "");
}

}  // namespace

int main(int argc, char* argv[]) {
    // Initialize logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    LOG(INFO) << "VEP Exporter IFEX starting...";

    // Parse configuration
    Config config = parse_args(argc, argv);
    log_config(config);

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create DDS participant
    LOG(INFO) << "Creating DDS participant...";
    dds::Participant participant;

    // Create transport (IFEX gRPC backend)
    LOG(INFO) << "Creating IFEX backend transport...";
    auto transport = std::make_unique<vep::IfexBackendTransport>(config.transport);

    // Create compressor
    auto comp_type = vep::exporter::compressor_type_from_string(config.compressor_type);
    if (!comp_type) {
        LOG(ERROR) << "Unknown compressor type: " << config.compressor_type;
        return 1;
    }
    LOG(INFO) << "Creating compressor (" << vep::exporter::to_string(*comp_type) << ")...";
    auto compressor = vep::exporter::create_compressor(*comp_type, config.compression_level);
    if (!compressor) {
        LOG(ERROR) << "Failed to create compressor";
        return 1;
    }

    // Create unified exporter pipeline (interleaved items in TransferBatch)
    LOG(INFO) << "Creating unified exporter pipeline...";
    vep::exporter::UnifiedExporterPipeline pipeline(
        std::move(transport),
        std::move(compressor),
        config.pipeline);

    if (!pipeline.start()) {
        LOG(ERROR) << "Failed to start exporter pipeline";
        return 1;
    }

    // Create subscription manager
    LOG(INFO) << "Creating subscription manager...";
    integration::SubscriptionManager sub_manager(participant, config.sub);

    // Wire callbacks to pipeline
    sub_manager.on_vss_signal([&pipeline](const vep_VssSignal& msg) {
        pipeline.send(msg);
    });

    sub_manager.on_event([&pipeline](const vep_Event& msg) {
        pipeline.send(msg);
    });

    sub_manager.on_gauge([&pipeline](const vep_OtelGauge& msg) {
        pipeline.send(msg);
    });

    sub_manager.on_counter([&pipeline](const vep_OtelCounter& msg) {
        pipeline.send(msg);
    });

    sub_manager.on_histogram([&pipeline](const vep_OtelHistogram& msg) {
        pipeline.send(msg);
    });

    sub_manager.on_log_entry([&pipeline](const vep_OtelLogEntry& msg) {
        pipeline.send(msg);
    });

    // Start receiving
    sub_manager.start();
    LOG(INFO) << "VEP Exporter IFEX running. Press Ctrl+C to stop.";

    // Main loop - periodic stats logging
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(30);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            last_stats_time = now;

            auto stats = pipeline.stats();
            LOG(INFO) << "Stats: items=" << stats.items_total
                      << " (signals=" << stats.signals_processed
                      << " events=" << stats.events_processed
                      << " metrics=" << stats.metrics_processed
                      << " logs=" << stats.logs_processed << ")"
                      << " batches=" << stats.batches_sent
                      << " compression=" << std::fixed << std::setprecision(1)
                      << (stats.compression_ratio() * 100.0) << "%";
        }
    }

    // Shutdown
    LOG(INFO) << "Shutting down...";
    sub_manager.stop();
    pipeline.stop();

    // Final stats
    auto stats = pipeline.stats();
    LOG(INFO) << "Final stats: items=" << stats.items_total
              << " (signals=" << stats.signals_processed
              << " events=" << stats.events_processed
              << " metrics=" << stats.metrics_processed
              << " logs=" << stats.logs_processed << ")"
              << " batches=" << stats.batches_sent
              << " compression=" << std::fixed << std::setprecision(1)
              << (stats.compression_ratio() * 100.0) << "%";

    LOG(INFO) << "VEP Exporter IFEX stopped.";
    return 0;
}
