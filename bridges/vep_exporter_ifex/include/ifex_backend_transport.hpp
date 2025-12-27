// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file ifex_backend_transport.hpp
/// @brief IFEX gRPC backend transport
///
/// Implements BackendTransport using IFEX BackendTransportClient.
/// Each instance is bound to a single content_id (gRPC channel metadata).
///
/// Architecture:
///   DDS → SubscriptionManager → UnifiedExporterPipeline → IfexBackendTransport
///                                                              ↓
///                                                  BackendTransportClient (gRPC)
///                                                              ↓
///                                                  grpc_backend_proxy (SOME/IP)
///                                                         or
///                                                  backend-transport-server (MQTT)

#include "vep/backend_transport.hpp"
#include "backend_transport_client.hpp"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace grpc {
class Channel;
}

namespace vep {

/// Configuration for IFEX backend transport
struct IfexBackendTransportConfig {
    std::string grpc_target = "localhost:50060";  ///< gRPC server address
    uint32_t content_id = 1;                       ///< Channel-bound content_id
};

/// IFEX gRPC backend transport using BackendTransportClient
///
/// This transport is bound to a single content_id. All publish calls must use
/// the configured content_id; mismatches are logged and rejected.
class IfexBackendTransport : public BackendTransport {
public:
    explicit IfexBackendTransport(const IfexBackendTransportConfig& config = {});
    ~IfexBackendTransport() override;

    IfexBackendTransport(const IfexBackendTransport&) = delete;
    IfexBackendTransport& operator=(const IfexBackendTransport&) = delete;

    // Lifecycle
    bool start() override;
    void stop() override;

    // v2c: publish (uses bound content_id)
    uint32_t content_id() const override { return config_.content_id; }
    bool publish(const std::vector<uint8_t>& data,
                 Persistence persistence = Persistence::BestEffort) override;

    // Status
    bool healthy() const override;
    ConnectionState connection_state() const override;
    bool queue_full() const override;
    BackendTransportStats stats() const override;
    std::string name() const override { return "ifex"; }

private:
    IfexBackendTransportConfig config_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<ifex::client::BackendTransportClient> client_;

    std::atomic<bool> running_{false};
    std::atomic<ConnectionState> connection_state_{ConnectionState::Disconnected};

    mutable std::mutex stats_mutex_;
    BackendTransportStats stats_;
};

}  // namespace vep
