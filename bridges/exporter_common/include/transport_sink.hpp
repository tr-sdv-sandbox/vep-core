// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file transport_sink.hpp
/// @brief Abstract interface for transport sinks (MQTT, SOME/IP, HTTP, etc.)
///
/// TransportSink decouples message transport from encoding/batching logic,
/// allowing different transport backends to be plugged in.

#include <cstdint>
#include <string>
#include <vector>

namespace vep::exporter {

/// Statistics for transport sinks
struct TransportStats {
    uint64_t messages_sent = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    int64_t last_send_timestamp_ns = 0;
};

/// Abstract interface for transport sinks.
///
/// Implementations handle the actual message delivery to external systems.
/// The sink receives already-encoded (and optionally compressed) binary data.
class TransportSink {
public:
    virtual ~TransportSink() = default;

    /// Start the sink (connect, allocate resources)
    /// @return true on success
    virtual bool start() = 0;

    /// Stop the sink (disconnect, release resources)
    virtual void stop() = 0;

    /// Publish data to a topic/channel
    /// @param topic Topic or channel name
    /// @param data Binary payload to send
    /// @return true if publish succeeded
    virtual bool publish(const std::string& topic, const std::vector<uint8_t>& data) = 0;

    /// Check if sink is healthy and ready
    virtual bool healthy() const = 0;

    /// Get statistics
    virtual TransportStats stats() const = 0;

    /// Get sink name for logging
    virtual std::string name() const = 0;
};

}  // namespace vep::exporter
