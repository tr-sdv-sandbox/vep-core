// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file backend_transport.hpp
/// @brief Abstract interface for bidirectional backend transport
///
/// BackendTransport provides a generic abstraction for services that need
/// bidirectional communication with a backend/cloud system. It decouples
/// transport implementation from application logic.
///
/// The interface uses content_id for routing:
/// - v2c (vehicle-to-cloud): publish(content_id, data)
/// - c2v (cloud-to-vehicle): subscribe_content(content_id) + on_content callback
///
/// Each implementation maps content_id to transport-specific addressing:
/// - MqttBackendTransport: content_id -> topic "v2c/{vehicle_id}/{content_id}"
/// - SomeipBackendTransport: content_id -> SOME/IP method via BE Message Proxy

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vep {

// =============================================================================
// Transport Types
// =============================================================================

/// Statistics for backend transport
struct BackendTransportStats {
    uint64_t messages_sent = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    uint64_t messages_received = 0;
    uint64_t bytes_received = 0;
    int64_t last_send_timestamp_ns = 0;
    int64_t last_receive_timestamp_ns = 0;
};

/// Connection state for backend transport
enum class ConnectionState : uint8_t {
    Unknown = 0,
    Connected = 1,
    Disconnected = 2,
    Connecting = 3,
    Reconnecting = 4
};

/// Connection status with reason
struct ConnectionStatus {
    ConnectionState state = ConnectionState::Unknown;
    std::string reason;
    int64_t timestamp_ns = 0;
};

/// Queue status for backpressure signaling
struct QueueStatus {
    bool is_full = false;
    uint32_t queue_size = 0;
    uint32_t queue_capacity = 0;
};

/// Message persistence level
///
/// Defines how long a message should be retained if delivery fails.
/// Higher persistence levels imply reliable delivery (retry until ack).
/// Transport implementations map these to their native persistence/QoS options.
enum class Persistence : uint8_t {
    None = 0,          ///< Fire and forget
    UntilDelivered,    ///< Retry until ack, then discard
    UntilRestart,      ///< Cleared on app/system restart
    Persistent         ///< Survives power cycles
};

// =============================================================================
// Callback Types
// =============================================================================

/// Callback for incoming content (c2v - cloud-to-vehicle)
/// @param content_id Application content identifier
/// @param payload Binary payload received
using ContentCallback = std::function<void(uint32_t content_id,
                                           const std::vector<uint8_t>& payload)>;

/// Callback for connection status changes
using ConnectionCallback = std::function<void(const ConnectionStatus& status)>;

/// Callback for queue status changes (backpressure)
using QueueStatusCallback = std::function<void(const QueueStatus& status)>;

// =============================================================================
// BackendTransport Interface
// =============================================================================

/// Abstract interface for bidirectional backend transport.
///
/// Implementations handle message delivery to/from external systems.
/// The transport receives already-encoded (and optionally compressed) binary data.
///
/// Bidirectional communication:
/// - v2c: Call publish(content_id, data) to send data
/// - c2v: Call subscribe_content() and set on_content() callback to receive data
class BackendTransport {
public:
    virtual ~BackendTransport() = default;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Start the transport (connect, allocate resources)
    /// @return true on success
    virtual bool start() = 0;

    /// Stop the transport (disconnect, release resources)
    virtual void stop() = 0;

    // =========================================================================
    // v2c: Vehicle-to-Cloud (publish)
    // =========================================================================

    /// Publish data for a specific content ID
    /// @param content_id Application content identifier
    /// @param data Binary payload to send
    /// @param persistence Message persistence level (default: fire and forget)
    /// @return true if publish succeeded
    virtual bool publish(uint32_t content_id,
                         const std::vector<uint8_t>& data,
                         Persistence persistence = Persistence::None) = 0;

    // =========================================================================
    // c2v: Cloud-to-Vehicle (subscribe/receive)
    // =========================================================================

    /// Subscribe to receive content for a specific content ID
    /// @param content_id Content identifier to subscribe to
    /// @note Received content delivered via on_content callback
    virtual void subscribe_content(uint32_t content_id) { (void)content_id; }

    /// Unsubscribe from a content ID
    /// @param content_id Content identifier to unsubscribe from
    virtual void unsubscribe_content(uint32_t content_id) { (void)content_id; }

    /// Set callback for incoming content (c2v)
    /// @param callback Function to call when content is received
    virtual void on_content(ContentCallback callback) { on_content_ = std::move(callback); }

    // =========================================================================
    // Status and Backpressure
    // =========================================================================

    /// Check if transport is healthy and ready
    virtual bool healthy() const = 0;

    /// Get current connection state
    virtual ConnectionState connection_state() const { return ConnectionState::Unknown; }

    /// Set callback for connection status changes
    virtual void on_connection_status(ConnectionCallback callback) {
        on_connection_status_ = std::move(callback);
    }

    /// Check if outbound queue is full (backpressure active)
    /// @return true if callers should throttle/pause publishing
    virtual bool queue_full() const { return false; }

    /// Set callback for queue status changes
    virtual void on_queue_status(QueueStatusCallback callback) {
        on_queue_status_ = std::move(callback);
    }

    // =========================================================================
    // Metadata
    // =========================================================================

    /// Get statistics
    virtual BackendTransportStats stats() const = 0;

    /// Get transport name for logging
    virtual std::string name() const = 0;

protected:
    ContentCallback on_content_;
    ConnectionCallback on_connection_status_;
    QueueStatusCallback on_queue_status_;
};

}  // namespace vep
