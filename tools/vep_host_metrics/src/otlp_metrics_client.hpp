// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// OTLP gRPC client for exporting metrics to an OpenTelemetry collector

#pragma once

#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "opentelemetry/proto/collector/metrics/v1/metrics_service.grpc.pb.h"
#include "opentelemetry/proto/metrics/v1/metrics.pb.h"

namespace vep {
namespace host_metrics {

/// Configuration for the OTLP client
struct OtlpConfig {
    std::string endpoint = "localhost:4317";
    std::string service_name = "vep_host_metrics";
    std::string service_namespace = "vep";
    std::string service_version = "0.1.0";
    std::string host_name;  // Defaults to actual hostname if empty
    std::chrono::seconds timeout{10};
    bool insecure = true;  // Use insecure channel (no TLS)
};

/// Lightweight OTLP gRPC client for metrics export
class OtlpMetricsClient {
public:
    explicit OtlpMetricsClient(const OtlpConfig& config);
    ~OtlpMetricsClient() = default;

    /// Wait for the endpoint to become available. Returns true if connected.
    /// If running_flag is provided, will check it periodically and return false if cleared.
    bool wait_for_connection(std::atomic<bool>* running_flag = nullptr,
                             int timeout_seconds = 30);

    /// Check if channel is currently connected
    bool is_connected() const;

    /// Export a batch of metrics. Returns true on success.
    bool export_metrics(
        opentelemetry::proto::metrics::v1::ResourceMetrics& resource_metrics);

    /// Create a ResourceMetrics message with resource attributes set
    opentelemetry::proto::metrics::v1::ResourceMetrics create_resource_metrics();

    /// Helper to add a gauge metric
    static void add_gauge(
        opentelemetry::proto::metrics::v1::ScopeMetrics* scope_metrics,
        const std::string& name,
        const std::string& description,
        const std::string& unit,
        double value,
        uint64_t timestamp_ns,
        const std::vector<std::pair<std::string, std::string>>& attributes = {});

    /// Helper to add a counter metric (as Sum with is_monotonic=true)
    static void add_counter(
        opentelemetry::proto::metrics::v1::ScopeMetrics* scope_metrics,
        const std::string& name,
        const std::string& description,
        const std::string& unit,
        int64_t value,
        uint64_t timestamp_ns,
        const std::vector<std::pair<std::string, std::string>>& attributes = {});

    /// Get current timestamp in nanoseconds
    static uint64_t now_ns();

private:
    OtlpConfig config_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<opentelemetry::proto::collector::metrics::v1::MetricsService::Stub> stub_;
    bool was_connected_ = false;  // Track connection state for log spam reduction
};

}  // namespace host_metrics
}  // namespace vep
