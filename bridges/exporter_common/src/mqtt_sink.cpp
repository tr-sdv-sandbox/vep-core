// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "mqtt_sink.hpp"

#include <glog/logging.h>
#include <mosquitto.h>

#include <chrono>

namespace vep::exporter {

MqttSink::MqttSink(const MqttConfig& config)
    : config_(config) {
}

MqttSink::~MqttSink() {
    stop();
}

bool MqttSink::start() {
    if (running_) {
        return true;
    }

    mosquitto_lib_init();

    mosq_ = mosquitto_new(config_.client_id.c_str(), true, this);
    if (!mosq_) {
        LOG(ERROR) << "Failed to create mosquitto client";
        return false;
    }

    mosquitto_connect_callback_set(mosq_, on_connect);
    mosquitto_disconnect_callback_set(mosq_, on_disconnect);

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
        return false;
    }

    mosquitto_loop_start(mosq_);
    running_ = true;

    LOG(INFO) << "MqttSink started, connecting to "
              << config_.broker_host << ":" << config_.broker_port;
    return true;
}

void MqttSink::stop() {
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

    connected_ = false;

    LOG(INFO) << "MqttSink stopped. Stats: sent=" << stats_.messages_sent
              << " failed=" << stats_.messages_failed
              << " bytes=" << stats_.bytes_sent;
}

bool MqttSink::publish(const std::string& topic, const std::vector<uint8_t>& data) {
    if (!connected_ || !mosq_) {
        LOG(WARNING) << "MQTT not connected, dropping message for topic: " << topic;
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.messages_failed++;
        return false;
    }

    std::string full_topic = config_.topic_prefix + "/" + topic;

    int rc = mosquitto_publish(mosq_, nullptr,
                               full_topic.c_str(),
                               static_cast<int>(data.size()),
                               data.data(),
                               config_.qos,
                               false);

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

    return true;
}

bool MqttSink::healthy() const {
    return running_ && connected_;
}

TransportStats MqttSink::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void MqttSink::on_connect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<MqttSink*>(obj);
    if (rc == 0) {
        self->connected_ = true;
        LOG(INFO) << "MqttSink connected to broker";
    } else {
        self->connected_ = false;
        LOG(WARNING) << "MQTT connection failed: " << mosquitto_connack_string(rc);
    }
}

void MqttSink::on_disconnect(struct mosquitto*, void* obj, int rc) {
    auto* self = static_cast<MqttSink*>(obj);
    self->connected_ = false;
    if (rc != 0) {
        LOG(WARNING) << "MQTT unexpected disconnect: " << mosquitto_strerror(rc);
    }
}

}  // namespace vep::exporter
