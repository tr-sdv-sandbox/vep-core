// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief VEP Exporter - Subscribes to DDS topics and exports via compressed MQTT
///
/// This application wires SubscriptionManager with UnifiedExporterPipeline
/// to create a bandwidth-efficient vehicle-to-cloud data export pipeline.
///
/// Architecture:
///   DDS Topics → SubscriptionManager → UnifiedExporterPipeline → TransportSink → MQTT
///
/// Usage:
///   vep_exporter [--config config.yaml]

#include "common/dds_wrapper.hpp"
#include "unified_pipeline.hpp"
#include "vep/mqtt_backend_transport.hpp"
#include "compressor.hpp"
#include "subscriber.hpp"

#include <glog/logging.h>
#include <yaml-cpp/yaml.h>

#include <atomic>
#include <csignal>
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
              << "VEP Exporter - Exports vehicle telemetry from DDS to cloud via MQTT\n"
              << "\n"
              << "Options:\n"
              << "  --config FILE     Load configuration from YAML file\n"
              << "  --broker HOST     MQTT broker host (default: localhost)\n"
              << "  --port PORT       MQTT broker port (default: 1883)\n"
              << "  --client-id ID    MQTT client ID (default: vep_exporter)\n"
              << "  --vehicle-id ID   Vehicle identifier for topic routing (required)\n"
              << "  --content-id N    Content ID for message routing (default: 1)\n"
              << "  --batch-size N    Max items per batch (default: 100)\n"
              << "  --batch-timeout MS  Batch timeout in ms (default: 1000)\n"
              << "  --compression N   Zstd compression level 1-19 (default: 3)\n"
              << "  --no-compression  Disable compression\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << prog << " --broker 192.168.1.100 --vehicle-id VIN123\n"
              << "\n";
}

struct Config {
    vep::MqttBackendTransportConfig mqtt;
    vep::exporter::UnifiedPipelineConfig pipeline;
    integration::SubscriptionConfig sub;
    std::string compressor_type = "zstd";
    int compression_level = 3;
};

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--config" && i + 1 < argc) {
            std::string config_file = argv[++i];
            try {
                YAML::Node yaml = YAML::LoadFile(config_file);

                if (yaml["mqtt"]) {
                    auto mqtt = yaml["mqtt"];
                    if (mqtt["broker"]) config.mqtt.broker_host = mqtt["broker"].as<std::string>();
                    if (mqtt["port"]) config.mqtt.broker_port = mqtt["port"].as<int>();
                    if (mqtt["client_id"]) config.mqtt.client_id = mqtt["client_id"].as<std::string>();
                    if (mqtt["username"]) config.mqtt.username = mqtt["username"].as<std::string>();
                    if (mqtt["password"]) config.mqtt.password = mqtt["password"].as<std::string>();
                    if (mqtt["vehicle_id"]) config.mqtt.vehicle_id = mqtt["vehicle_id"].as<std::string>();
                    if (mqtt["qos"]) config.mqtt.qos = mqtt["qos"].as<int>();
                }

                if (yaml["batching"]) {
                    auto batch = yaml["batching"];
                    if (batch["max_items"]) config.pipeline.batch_max_items = batch["max_items"].as<size_t>();
                    if (batch["max_bytes"]) config.pipeline.batch_max_bytes = batch["max_bytes"].as<size_t>();
                    if (batch["timeout_ms"]) {
                        config.pipeline.batch_timeout = std::chrono::milliseconds(batch["timeout_ms"].as<int>());
                    }
                    if (batch["content_id"]) config.pipeline.content_id = batch["content_id"].as<uint32_t>();
                }

                if (yaml["compression"]) {
                    auto comp = yaml["compression"];
                    if (comp["type"]) config.compressor_type = comp["type"].as<std::string>();
                    if (comp["level"]) config.compression_level = comp["level"].as<int>();
                }

                if (yaml["subscriptions"]) {
                    auto subs = yaml["subscriptions"];
                    if (subs["vss_signals"]) config.sub.vss_signals = subs["vss_signals"].as<bool>();
                    if (subs["events"]) config.sub.events = subs["events"].as<bool>();
                    if (subs["gauges"]) config.sub.gauges = subs["gauges"].as<bool>();
                    if (subs["counters"]) config.sub.counters = subs["counters"].as<bool>();
                    if (subs["histograms"]) config.sub.histograms = subs["histograms"].as<bool>();
                    if (subs["logs"]) config.sub.logs = subs["logs"].as<bool>();
                }

                LOG(INFO) << "Loaded configuration from " << config_file;
            } catch (const std::exception& e) {
                LOG(ERROR) << "Failed to load config file: " << e.what();
                exit(1);
            }
        } else if (arg == "--broker" && i + 1 < argc) {
            config.mqtt.broker_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.mqtt.broker_port = std::stoi(argv[++i]);
        } else if (arg == "--client-id" && i + 1 < argc) {
            config.mqtt.client_id = argv[++i];
        } else if (arg == "--vehicle-id" && i + 1 < argc) {
            config.mqtt.vehicle_id = argv[++i];
        } else if (arg == "--content-id" && i + 1 < argc) {
            config.pipeline.content_id = static_cast<uint32_t>(std::stoul(argv[++i]));
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
    LOG(INFO) << "=== VEP Exporter Configuration ===";
    LOG(INFO) << "MQTT Broker: " << config.mqtt.broker_host << ":" << config.mqtt.broker_port;
    LOG(INFO) << "Client ID: " << config.mqtt.client_id;
    LOG(INFO) << "Vehicle ID: " << config.mqtt.vehicle_id;
    LOG(INFO) << "Content ID: " << config.pipeline.content_id;
    LOG(INFO) << "Batching: " << config.pipeline.batch_max_items << " items, "
              << config.pipeline.batch_timeout.count() << "ms timeout";
    LOG(INFO) << "Compression: " << config.compressor_type
              << (config.compressor_type == "zstd" ? " (level " + std::to_string(config.compression_level) + ")" : "");
    LOG(INFO) << "Subscriptions: vss=" << config.sub.vss_signals
              << " events=" << config.sub.events
              << " gauges=" << config.sub.gauges
              << " counters=" << config.sub.counters
              << " histograms=" << config.sub.histograms
              << " logs=" << config.sub.logs;
}

}  // namespace

int main(int argc, char* argv[]) {
    // Initialize logging
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;
    FLAGS_colorlogtostderr = true;

    LOG(INFO) << "VEP Exporter starting...";

    // Parse configuration
    Config config = parse_args(argc, argv);
    log_config(config);

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create DDS participant
    LOG(INFO) << "Creating DDS participant...";
    dds::Participant participant;

    // Create transport (MQTT backend)
    LOG(INFO) << "Creating MQTT transport...";
    auto transport = std::make_unique<vep::MqttBackendTransport>(config.mqtt);

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
    LOG(INFO) << "VEP Exporter running. Press Ctrl+C to stop.";

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

    LOG(INFO) << "VEP Exporter stopped.";
    return 0;
}
