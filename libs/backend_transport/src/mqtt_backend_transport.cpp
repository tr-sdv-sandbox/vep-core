// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "vep/mqtt_backend_transport.hpp"

#include <glog/logging.h>
#include <mosquitto.h>

#include <chrono>

namespace vep {

MqttBackendTransport::MqttBackendTransport(const MqttBackendTransportConfig& config)
    : config_(config) {
}

MqttBackendTransport::~MqttBackendTransport() {
    stop();
}

bool MqttBackendTransport::start() {
    if (running_) {
        return true;
    }

    mosquitto_lib_init();

    mosq_ = mosquitto_new(config_.client_id.c_str(), true, this);
    if (!mosq_) {
        LOG(ERROR) << "Failed to create mosquitto client";
        return false;
    }

    connection_state_ = ConnectionState::Connecting;

    mosquitto_connect_callback_set(mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);
    mosquitto_message_callback_set(mosq_, on_message);

    if (!config_.username.empty()) {
        mosquitto_username_pw_set(mosq_, config_.username.c_str(),
                                  config_.password.c_str());
    }

    int rc = mosquitto_connect_async(mosq_, config_.broker_host.c_str(),
                                      config_.broker_port, config_.keepalive_sec);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(ERROR) << "MQTT connect failed: " << rc << ", " << mosquitto_strerror(rc);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        connection_state_ = ConnectionState::Disconnected;
        return false;
    }

    mosquitto_loop_start(mosq_);
    running_ = true;

    LOG(INFO) << "MqttBackendTransport started, connecting to "
              << config_.broker_host << ":" << config_.broker_port;
    return true;
}

void MqttBackendTransport::stop() {
    if (!running_) {
        return;
    }

    running_ = false;

    if (mosq_) {
        mosquitto_disconnect(mosq_);
        mosquitto_loop_stop(mosq_, false);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
    }
    mosquitto_lib_cleanup();

    connection_state_ = ConnectionState::Disconnected;

    LOG(INFO) << "MqttBackendTransport stopped. Stats: sent=" << stats_.messages_sent
              << " failed=" << stats_.messages_failed
              << " bytes=" << stats_.bytes_sent
              << " received=" << stats_.messages_received;
}

bool MqttBackendTransport::publish(uint32_t content_id,
                                    const std::vector<uint8_t>& data,
                                    Persistence persistence) {
    if (connection_state_ != ConnectionState::Connected || !mosq_) {
        LOG(WARNING) << "MQTT not connected, dropping message for content_id: " << content_id;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_failed++;
        return false;
    }

    std::string topic = v2c_topic(content_id);

    // Map persistence to MQTT QoS
    // None → QoS 0, UntilDelivered+ → at least QoS 1
    int qos = config_.qos;
    if (persistence == Persistence::None) {
        qos = 0;
    } else if (persistence >= Persistence::UntilDelivered && qos < 1) {
        qos = 1;
    }

    // Retain flag for persistent messages
    bool retain = (persistence == Persistence::Persistent);

    int rc = mosquitto_publish(mosq_, nullptr,
                               topic.c_str(),
                               static_cast<int>(data.size()),
                               data.data(),
                               qos,
                               retain);

    if (rc != MOSQ_ERR_SUCCESS) {
        LOG_EVERY_N(WARNING, 100) << "MQTT publish failed: " << mosquitto_strerror(rc);
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_failed++;
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_sent++;
        stats_.bytes_sent += data.size();
        stats_.last_send_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    VLOG(2) << "Published to " << topic << " (" << data.size() << " bytes)";
    return true;
}

void MqttBackendTransport::subscribe_content(uint32_t content_id) {
    if (!mosq_) {
        LOG(WARNING) << "Cannot subscribe: MQTT not initialized";
        return;
    }

    std::string topic = c2v_topic(content_id);

    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        if (subscribed_content_ids_.count(content_id) > 0) {
            return;  // Already subscribed
        }
        subscribed_content_ids_.insert(content_id);
    }

    int rc = mosquitto_subscribe(mosq_, nullptr, topic.c_str(), config_.qos);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(ERROR) << "MQTT subscribe failed for " << topic << ": " << mosquitto_strerror(rc);
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_content_ids_.erase(content_id);
    } else {
        LOG(INFO) << "Subscribed to c2v topic: " << topic;
    }
}

void MqttBackendTransport::unsubscribe_content(uint32_t content_id) {
    if (!mosq_) {
        return;
    }

    std::string topic = c2v_topic(content_id);

    {
        std::lock_guard<std::mutex> lock(subscriptions_mutex_);
        subscribed_content_ids_.erase(content_id);
    }

    int rc = mosquitto_unsubscribe(mosq_, nullptr, topic.c_str());
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(WARNING) << "MQTT unsubscribe failed for " << topic << ": " << mosquitto_strerror(rc);
    } else {
        LOG(INFO) << "Unsubscribed from c2v topic: " << topic;
    }
}

bool MqttBackendTransport::healthy() const {
    return running_ && connection_state_ == ConnectionState::Connected;
}

ConnectionState MqttBackendTransport::connection_state() const {
    return connection_state_;
}

BackendTransportStats MqttBackendTransport::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

// =============================================================================
// Topic Helpers
// =============================================================================

std::string MqttBackendTransport::v2c_topic(uint32_t content_id) const {
    return config_.v2c_prefix + "/" + config_.vehicle_id + "/" + std::to_string(content_id);
}

std::string MqttBackendTransport::c2v_topic(uint32_t content_id) const {
    return config_.c2v_prefix + "/" + config_.vehicle_id + "/" + std::to_string(content_id);
}

std::string MqttBackendTransport::c2v_wildcard_topic() const {
    return config_.c2v_prefix + "/" + config_.vehicle_id + "/+";
}

bool MqttBackendTransport::parse_c2v_topic(const std::string& topic, uint32_t& content_id) const {
    // Parse topic like "c2v/{vehicle_id}/{content_id}"
    std::string prefix = config_.c2v_prefix + "/" + config_.vehicle_id + "/";
    if (topic.find(prefix) != 0) {
        return false;
    }

    std::string content_id_str = topic.substr(prefix.length());
    try {
        content_id = static_cast<uint32_t>(std::stoul(content_id_str));
        return true;
    } catch (...) {
        return false;
    }
}

// =============================================================================
// Mosquitto Callbacks
// =============================================================================

void MqttBackendTransport::on_connect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<MqttBackendTransport*>(obj);

    if (rc == 0) {
        self->connection_state_ = ConnectionState::Connected;
        LOG(INFO) << "MqttBackendTransport connected to broker";

        // Notify via callback
        if (self->on_connection_status_) {
            ConnectionStatus status;
            status.state = ConnectionState::Connected;
            status.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            self->on_connection_status_(status);
        }

        // Subscribe to configured content IDs
        for (uint32_t content_id : self->config_.content_ids) {
            self->subscribe_content(content_id);
        }
    } else {
        self->connection_state_ = ConnectionState::Disconnected;
        LOG(WARNING) << "MQTT connection failed: " << mosquitto_connack_string(rc);

        if (self->on_connection_status_) {
            ConnectionStatus status;
            status.state = ConnectionState::Disconnected;
            status.reason = mosquitto_connack_string(rc);
            status.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            self->on_connection_status_(status);
        }
    }
}

void MqttBackendTransport::on_disconnect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<MqttBackendTransport*>(obj);

    if (self->running_) {
        self->connection_state_ = ConnectionState::Reconnecting;
    } else {
        self->connection_state_ = ConnectionState::Disconnected;
    }

    if (rc != 0) {
        LOG(WARNING) << "MQTT unexpected disconnect: " << mosquitto_strerror(rc);
    }

    if (self->on_connection_status_) {
        ConnectionStatus status;
        status.state = self->connection_state_;
        status.reason = (rc != 0) ? mosquitto_strerror(rc) : "normal";
        status.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        self->on_connection_status_(status);
    }
}

void MqttBackendTransport::on_message(struct mosquitto*, void* obj,
                                       const struct mosquitto_message* msg) {
    auto* self = static_cast<MqttBackendTransport*>(obj);

    if (!msg || !msg->payload || msg->payloadlen == 0) {
        return;
    }

    // Update stats
    {
        std::lock_guard<std::mutex> lock(self->stats_mutex_);
        self->stats_.messages_received++;
        self->stats_.bytes_received += msg->payloadlen;
        self->stats_.last_receive_timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    // Parse content_id from topic
    uint32_t content_id;
    if (!self->parse_c2v_topic(msg->topic, content_id)) {
        LOG(WARNING) << "Could not parse content_id from topic: " << msg->topic;
        return;
    }

    VLOG(2) << "Received c2v message: content_id=" << content_id
            << ", size=" << msg->payloadlen;

    // Deliver to callback
    if (self->on_content_) {
        std::vector<uint8_t> payload(
            static_cast<uint8_t*>(msg->payload),
            static_cast<uint8_t*>(msg->payload) + msg->payloadlen);
        self->on_content_(content_id, payload);
    }
}

}  // namespace vep
