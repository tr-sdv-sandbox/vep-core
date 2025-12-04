// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief VDR Exporter - Subscribes to DDS topics and exports via compressed MQTT
///
/// This application wires vdr-light's SubscriptionManager with our
/// CompressedMqttSink to create a bandwidth-efficient vehicle-to-cloud
/// data export pipeline.
///
/// Architecture:
///   DDS Topics → SubscriptionManager → CompressedMqttSink → MQTT Broker
///
/// Usage:
///   vdr_exporter [--config config.yaml]

#include "common/dds_wrapper.hpp"
#include "exporter/compressed_mqtt_sink.hpp"
#include "exporter/subscriber.hpp"
#include "vss_signal.h"

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
              << "VDR Exporter - Exports vehicle telemetry from DDS to cloud via MQTT\n"
              << "\n"
              << "Options:\n"
              << "  --config FILE     Load configuration from YAML file\n"
              << "  --broker HOST     MQTT broker host (default: localhost)\n"
              << "  --port PORT       MQTT broker port (default: 1883)\n"
              << "  --client-id ID    MQTT client ID (default: vdr_exporter)\n"
              << "  --batch-size N    Max signals per batch (default: 100)\n"
              << "  --batch-timeout MS  Batch timeout in ms (default: 1000)\n"
              << "  --compression N   Zstd compression level 1-19 (default: 3)\n"
              << "  --help            Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << prog << " --broker 192.168.1.100 --port 1883\n"
              << "\n";
}

struct Config {
    integration::CompressedMqttConfig mqtt;
    integration::SubscriptionConfig sub;
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
                    if (mqtt["topic_prefix"]) config.mqtt.topic_prefix = mqtt["topic_prefix"].as<std::string>();
                    if (mqtt["qos"]) config.mqtt.qos = mqtt["qos"].as<int>();
                }

                if (yaml["batching"]) {
                    auto batch = yaml["batching"];
                    if (batch["max_signals"]) config.mqtt.batch_max_signals = batch["max_signals"].as<size_t>();
                    if (batch["max_events"]) config.mqtt.batch_max_events = batch["max_events"].as<size_t>();
                    if (batch["max_metrics"]) config.mqtt.batch_max_metrics = batch["max_metrics"].as<size_t>();
                    if (batch["timeout_ms"]) {
                        config.mqtt.batch_timeout = std::chrono::milliseconds(batch["timeout_ms"].as<int>());
                    }
                }

                if (yaml["compression"]) {
                    auto comp = yaml["compression"];
                    if (comp["level"]) config.mqtt.zstd_compression_level = comp["level"].as<int>();
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
        } else if (arg == "--batch-size" && i + 1 < argc) {
            config.mqtt.batch_max_signals = std::stoul(argv[++i]);
        } else if (arg == "--batch-timeout" && i + 1 < argc) {
            config.mqtt.batch_timeout = std::chrono::milliseconds(std::stoi(argv[++i]));
        } else if (arg == "--compression" && i + 1 < argc) {
            config.mqtt.zstd_compression_level = std::stoi(argv[++i]);
        } else {
            LOG(WARNING) << "Unknown argument: " << arg;
        }
    }

    return config;
}

void log_config(const Config& config) {
    LOG(INFO) << "=== VDR Exporter Configuration ===";
    LOG(INFO) << "MQTT Broker: " << config.mqtt.broker_host << ":" << config.mqtt.broker_port;
    LOG(INFO) << "Client ID: " << config.mqtt.client_id;
    LOG(INFO) << "Topic prefix: " << config.mqtt.topic_prefix;
    LOG(INFO) << "Batching: " << config.mqtt.batch_max_signals << " signals, "
              << config.mqtt.batch_timeout.count() << "ms timeout";
    LOG(INFO) << "Compression level: " << config.mqtt.zstd_compression_level;
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

    LOG(INFO) << "VDR Exporter starting...";

    // Parse configuration
    Config config = parse_args(argc, argv);
    log_config(config);

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create DDS participant
    LOG(INFO) << "Creating DDS participant...";
    dds::Participant participant;

    // Create compressed MQTT sink
    LOG(INFO) << "Creating compressed MQTT sink...";
    integration::CompressedMqttSink mqtt_sink(config.mqtt);

    if (!mqtt_sink.start()) {
        LOG(ERROR) << "Failed to start MQTT sink";
        return 1;
    }

    // Create subscription manager
    LOG(INFO) << "Creating subscription manager...";
    integration::SubscriptionManager sub_manager(participant, config.sub);

    // Wire callbacks to sink
    sub_manager.on_vss_signal([&mqtt_sink](const vss_Signal& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_event([&mqtt_sink](const telemetry_events_Event& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_gauge([&mqtt_sink](const telemetry_metrics_Gauge& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_counter([&mqtt_sink](const telemetry_metrics_Counter& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_histogram([&mqtt_sink](const telemetry_metrics_Histogram& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_log_entry([&mqtt_sink](const telemetry_logs_LogEntry& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_scalar_measurement([&mqtt_sink](const telemetry_diagnostics_ScalarMeasurement& msg) {
        mqtt_sink.send(msg);
    });

    sub_manager.on_vector_measurement([&mqtt_sink](const telemetry_diagnostics_VectorMeasurement& msg) {
        mqtt_sink.send(msg);
    });

    // Start receiving
    sub_manager.start();
    LOG(INFO) << "VDR Exporter running. Press Ctrl+C to stop.";

    // Main loop - periodic stats logging
    auto last_stats_time = std::chrono::steady_clock::now();
    const auto stats_interval = std::chrono::seconds(30);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= stats_interval) {
            last_stats_time = now;

            auto stats = mqtt_sink.stats();
            auto comp_stats = mqtt_sink.compression_stats();

            LOG(INFO) << "Stats: sent=" << stats.messages_sent
                      << " failed=" << stats.messages_failed
                      << " bytes=" << stats.bytes_sent
                      << " batches=" << comp_stats.batches_sent
                      << " compression=" << std::fixed << std::setprecision(2)
                      << (comp_stats.compression_ratio() * 100.0) << "%";
        }
    }

    // Shutdown
    LOG(INFO) << "Shutting down...";
    sub_manager.stop();
    mqtt_sink.flush();
    mqtt_sink.stop();

    // Final stats
    auto stats = mqtt_sink.stats();
    auto comp_stats = mqtt_sink.compression_stats();
    LOG(INFO) << "Final stats: sent=" << stats.messages_sent
              << " failed=" << stats.messages_failed
              << " bytes=" << stats.bytes_sent
              << " batches=" << comp_stats.batches_sent
              << " compression=" << std::fixed << std::setprecision(2)
              << (comp_stats.compression_ratio() * 100.0) << "%";

    LOG(INFO) << "VDR Exporter stopped.";
    return 0;
}
