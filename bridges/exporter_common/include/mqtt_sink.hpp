// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file mqtt_sink.hpp
/// @brief MQTT transport sink implementation

#include "transport_sink.hpp"

#include <atomic>
#include <mutex>
#include <string>

// Forward declaration
struct mosquitto;

namespace vep::exporter {

/// Configuration for MQTT sink
struct MqttConfig {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string client_id = "vep_exporter";
    std::string username;
    std::string password;
    int keepalive_sec = 60;
    int qos = 1;
    std::string topic_prefix = "v1/telemetry";
};

/// MQTT transport sink implementation
class MqttSink : public TransportSink {
public:
    explicit MqttSink(const MqttConfig& config = {});
    ~MqttSink() override;

    MqttSink(const MqttSink&) = delete;
    MqttSink& operator=(const MqttSink&) = delete;

    bool start() override;
    void stop() override;
    bool publish(const std::string& topic, const std::vector<uint8_t>& data) override;
    bool healthy() const override;
    TransportStats stats() const override;
    std::string name() const override { return "mqtt"; }

private:
    static void on_connect(struct mosquitto* mosq, void* obj, int rc);
    static void on_disconnect(struct mosquitto* mosq, void* obj, int rc);

    MqttConfig config_;
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    TransportStats stats_;
};

}  // namespace vep::exporter
