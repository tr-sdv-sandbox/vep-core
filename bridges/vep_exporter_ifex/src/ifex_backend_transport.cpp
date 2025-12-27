// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "ifex_backend_transport.hpp"

#include <grpcpp/grpcpp.h>
#include <glog/logging.h>
#include <chrono>

namespace vep {

namespace {

// Types are aligned with IFEX - simple cast conversions
inline ifex::client::Persistence to_ifex(Persistence p) {
    return static_cast<ifex::client::Persistence>(p);
}

inline ConnectionState from_ifex(ifex::client::ConnectionState s) {
    return static_cast<ConnectionState>(s);
}

inline DisconnectReason from_ifex(ifex::client::DisconnectReason r) {
    return static_cast<DisconnectReason>(r);
}

inline QueueLevel from_ifex(ifex::client::QueueLevel l) {
    return static_cast<QueueLevel>(l);
}

int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

}  // namespace

IfexBackendTransport::IfexBackendTransport(const IfexBackendTransportConfig& config)
    : config_(config) {
}

IfexBackendTransport::~IfexBackendTransport() {
    stop();
}

bool IfexBackendTransport::start() {
    if (running_) return true;

    // Create gRPC channel
    channel_ = grpc::CreateChannel(config_.grpc_target,
                                   grpc::InsecureChannelCredentials());

    if (!channel_) {
        LOG(ERROR) << "IfexBackendTransport: Failed to create gRPC channel to "
                   << config_.grpc_target;
        return false;
    }

    // Create BackendTransportClient (channel-bound to content_id)
    client_ = std::make_unique<ifex::client::BackendTransportClient>(
        channel_, config_.content_id);

    // Set up connection status callback
    client_->on_connection_changed([this](const ifex::client::ConnectionStatus& status) {
        ConnectionState new_state = from_ifex(status.state);
        connection_state_ = new_state;

        LOG(INFO) << "IfexBackendTransport: Connection state changed to "
                  << static_cast<int>(new_state);

        if (on_connection_status_) {
            ConnectionStatus vep_status;
            vep_status.state = new_state;
            vep_status.reason = from_ifex(status.reason);
            vep_status.timestamp_ns = status.timestamp_ns;
            on_connection_status_(vep_status);
        }
    });

    // Set up content callback for c2v
    client_->on_content([this](const std::vector<uint8_t>& payload) {
        // Update stats
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.messages_received++;
            stats_.bytes_received += payload.size();
            stats_.last_receive_timestamp_ns = now_ns();
        }

        // Forward to user callback with our configured content_id
        if (on_content_) {
            on_content_(config_.content_id, payload);
        }
    });

    running_ = true;
    connection_state_ = ConnectionState::Connecting;

    LOG(INFO) << "IfexBackendTransport: Started (target=" << config_.grpc_target
              << ", content_id=" << config_.content_id << ")";
    return true;
}

void IfexBackendTransport::stop() {
    if (!running_) return;

    running_ = false;

    if (client_) {
        client_->unsubscribe_all();
        client_.reset();
    }

    channel_.reset();
    connection_state_ = ConnectionState::Disconnected;

    LOG(INFO) << "IfexBackendTransport: Stopped";
}

bool IfexBackendTransport::publish(const std::vector<uint8_t>& data,
                                    Persistence persistence) {
    if (!running_ || !client_) {
        return false;
    }

    // Publish using bound content_id (types are aligned - direct cast)
    ifex::client::PublishResult result = client_->publish(data, to_ifex(persistence));

    // Update stats
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (result.ok()) {
            stats_.messages_sent++;
            stats_.bytes_sent += data.size();
            stats_.last_send_timestamp_ns = now_ns();
        } else {
            stats_.messages_failed++;
        }
    }

    // Forward queue status if callback is set
    if (on_queue_status_) {
        QueueStatus qs;
        qs.level = from_ifex(result.queue_level);
        on_queue_status_(qs);
    }

    return result.ok();
}

bool IfexBackendTransport::healthy() const {
    return running_ && client_ && client_->healthy();
}

ConnectionState IfexBackendTransport::connection_state() const {
    return connection_state_;
}

bool IfexBackendTransport::queue_full() const {
    if (!client_) return false;
    return client_->queue_status().level == ifex::client::QueueLevel::Full;
}

BackendTransportStats IfexBackendTransport::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace vep
