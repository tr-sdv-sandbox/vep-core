// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file mqtt_backend_transport.hpp
/// @brief Bidirectional MQTT backend transport implementation
///
/// MqttBackendTransport implements BackendTransport for direct MQTT communication:
/// - v2c: publish(content_id, data) -> MQTT topic "v2c/{vehicle_id}/{content_id}"
/// - c2v: subscribe to "c2v/{vehicle_id}/+" -> on_content callback

#include "vep/backend_transport.hpp"

#include <atomic>
#include <mutex>
#include <set>
#include <string>

// Forward declarations (mosquitto types are in global namespace)
struct mosquitto;
struct mosquitto_message;

namespace vep {

/// Configuration for MQTT backend transport
struct MqttBackendTransportConfig {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string client_id = "vep_client";
    std::string username;
    std::string password;
    int keepalive_sec = 60;
    int qos = 1;

    // Vehicle identification for topic construction
    std::string vehicle_id;

    // Topic patterns (content_id appended for v2c, wildcard for c2v)
    std::string v2c_prefix = "v2c";   // v2c/{vehicle_id}/{content_id}
    std::string c2v_prefix = "c2v";   // c2v/{vehicle_id}/{content_id}

    // Content IDs to subscribe to on startup (c2v)
    std::vector<uint32_t> content_ids;
};

/// Bidirectional MQTT backend transport implementation
class MqttBackendTransport : public BackendTransport {
public:
    explicit MqttBackendTransport(const MqttBackendTransportConfig& config = {});
    ~MqttBackendTransport() override;

    MqttBackendTransport(const MqttBackendTransport&) = delete;
    MqttBackendTransport& operator=(const MqttBackendTransport&) = delete;

    // Lifecycle
    bool start() override;
    void stop() override;

    // v2c: publish
    bool publish(uint32_t content_id,
                 const std::vector<uint8_t>& data,
                 Persistence persistence = Persistence::None) override;

    // c2v: subscribe/receive
    void subscribe_content(uint32_t content_id) override;
    void unsubscribe_content(uint32_t content_id) override;

    // Status
    bool healthy() const override;
    ConnectionState connection_state() const override;
    BackendTransportStats stats() const override;
    std::string name() const override { return "mqtt"; }

private:
    // Mosquitto callbacks
    static void on_connect(struct mosquitto* mosq, void* obj, int rc);
    static void on_disconnect(struct mosquitto* mosq, void* obj, int rc);
    static void on_message(struct mosquitto* mosq, void* obj,
                           const struct mosquitto_message* msg);

    // Topic helpers
    std::string v2c_topic(uint32_t content_id) const;
    std::string c2v_topic(uint32_t content_id) const;
    std::string c2v_wildcard_topic() const;

    // Extract content_id from c2v topic
    bool parse_c2v_topic(const std::string& topic, uint32_t& content_id) const;

    MqttBackendTransportConfig config_;
    struct mosquitto* mosq_ = nullptr;
    std::atomic<bool> running_{false};
    std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};

    // Subscribed content IDs
    std::mutex subscriptions_mutex_;
    std::set<uint32_t> subscribed_content_ids_;

    mutable std::mutex stats_mutex_;
    BackendTransportStats stats_;
};

}  // namespace vep
