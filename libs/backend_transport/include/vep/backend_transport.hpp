// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file backend_transport.hpp
/// @brief Abstract interface for bidirectional backend transport
///
/// BackendTransport provides a generic abstraction for services that need
/// bidirectional communication with a backend/cloud system. It decouples
/// transport implementation from application logic.
///
/// Each transport instance is bound to a single content_id at construction:
/// - v2c (vehicle-to-cloud): publish(data) sends to bound content_id
/// - c2v (cloud-to-vehicle): on_content callback receives data
///
/// Each implementation maps content_id to transport-specific addressing:
/// - MqttBackendTransport: content_id -> topic "v2c/{vehicle_id}/{content_id}"
/// - SomeipBackendTransport: content_id -> SOME/IP method via BE Message Proxy
/// - IfexBackendTransport: content_id -> gRPC channel metadata

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace vep {

// =============================================================================
// Transport Types (aligned with IFEX BackendTransport)
// =============================================================================

/// Message persistence level (matches IFEX persistence_t)
///
/// Defines how long a message should be retained if delivery fails.
/// Higher persistence levels imply reliable delivery (retry until ack).
enum class Persistence : uint8_t {
    BestEffort = 0,  ///< Queued for ordering, pruned if stale. No retry on failure.
    Volatile = 1,    ///< Retry until delivered. Memory queue, lost on any shutdown.
    Durable = 2      ///< Retry until delivered. Persisted on graceful shutdown only.
};

/// Connection state (matches IFEX connection_state_t)
enum class ConnectionState : uint8_t {
    Unknown = 0,
    Connected = 1,
    Disconnected = 2,
    Connecting = 3,
    Reconnecting = 4
};

/// Disconnect reason (matches IFEX disconnect_reason_t)
enum class DisconnectReason : uint8_t {
    None = 0,                  ///< No disconnect (connected)
    Requested = 1,             ///< Client or service requested disconnect
    NetworkError = 2,          ///< Network failure
    BrokerUnavailable = 3,     ///< Cannot reach broker
    AuthenticationFailed = 4,  ///< Credentials rejected
    ProtocolError = 5,         ///< Protocol violation
    TlsError = 6               ///< TLS handshake failure
};

/// Connection status with reason
struct ConnectionStatus {
    ConnectionState state = ConnectionState::Unknown;
    DisconnectReason reason = DisconnectReason::None;
    int64_t timestamp_ns = 0;
};

/// Queue fill level for adaptive throttling (matches IFEX queue_level_t)
enum class QueueLevel : uint8_t {
    Empty = 0,     ///< 0%
    Low = 1,       ///< < 25%
    Normal = 2,    ///< 25-50%
    High = 3,      ///< 50-75% - consider throttling
    Critical = 4,  ///< 75-95% - throttle low-priority
    Full = 5       ///< > 95%
};

/// Queue status for adaptive throttling (matches IFEX queue_status_t)
struct QueueStatus {
    QueueLevel level = QueueLevel::Empty;
    uint32_t queue_size = 0;
    uint32_t queue_capacity = 0;

    /// Convenience: check if queue is full
    bool is_full() const { return level == QueueLevel::Full; }
};

/// Transport statistics (matches IFEX transport_stats_t)
struct TransportStats {
    uint64_t messages_sent = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    uint64_t messages_received = 0;
    uint64_t bytes_received = 0;
    int64_t last_send_timestamp_ns = 0;
    int64_t last_receive_timestamp_ns = 0;
};

/// @deprecated Use TransportStats instead
using BackendTransportStats = TransportStats;

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
/// Each transport instance is bound to a single content_id at construction.
/// The transport receives already-encoded (and optionally compressed) binary data.
///
/// Bidirectional communication:
/// - v2c: Call publish(data) to send data (uses bound content_id)
/// - c2v: Set on_content() callback to receive data
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

    /// Get the content_id this transport is bound to
    virtual uint32_t content_id() const = 0;

    /// Publish data using the bound content_id
    /// @param data Binary payload to send
    /// @param persistence Message persistence level (default: best effort)
    /// @return true if publish succeeded
    virtual bool publish(const std::vector<uint8_t>& data,
                         Persistence persistence = Persistence::BestEffort) = 0;

    // =========================================================================
    // c2v: Cloud-to-Vehicle (receive)
    // =========================================================================

    /// Set callback for incoming content (c2v)
    /// @param callback Function to call when content is received
    /// @note Set this BEFORE calling start() to avoid missing messages.
    ///       The transport auto-subscribes to its bound content_id on start().
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
