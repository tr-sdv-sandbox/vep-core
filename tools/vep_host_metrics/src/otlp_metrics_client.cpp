// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "otlp_metrics_client.hpp"

#include <glog/logging.h>
#include <unistd.h>

namespace vep {
namespace host_metrics {

OtlpMetricsClient::OtlpMetricsClient(const OtlpConfig& config)
    : config_(config) {
    // Get hostname if not specified
    if (config_.host_name.empty()) {
        char hostname[256];
        if (gethostname(hostname, sizeof(hostname)) == 0) {
            config_.host_name = hostname;
        } else {
            config_.host_name = "unknown";
        }
    }

    // Create channel
    if (config_.insecure) {
        channel_ = grpc::CreateChannel(
            config_.endpoint, grpc::InsecureChannelCredentials());
    } else {
        channel_ = grpc::CreateChannel(
            config_.endpoint, grpc::SslCredentials(grpc::SslCredentialsOptions()));
    }

    stub_ = opentelemetry::proto::collector::metrics::v1::MetricsService::NewStub(channel_);
    LOG(INFO) << "OTLP client initialized, endpoint: " << config_.endpoint;
}

bool OtlpMetricsClient::wait_for_connection(std::atomic<bool>* running_flag,
                                            int timeout_seconds) {
    LOG(INFO) << "Waiting for OTLP endpoint " << config_.endpoint << "...";

    auto deadline = std::chrono::system_clock::now() + std::chrono::seconds(timeout_seconds);

    while (std::chrono::system_clock::now() < deadline) {
        // Check if we should stop
        if (running_flag && !running_flag->load()) {
            return false;
        }

        // Try to connect
        auto state = channel_->GetState(true);  // true = try to connect
        if (state == GRPC_CHANNEL_READY) {
            LOG(INFO) << "Connected to OTLP endpoint " << config_.endpoint;
            return true;
        }

        // Wait for state change with short timeout
        channel_->WaitForStateChange(state,
            std::chrono::system_clock::now() + std::chrono::seconds(2));
    }

    LOG(WARNING) << "Timeout waiting for OTLP endpoint " << config_.endpoint;
    return false;
}

bool OtlpMetricsClient::is_connected() const {
    auto state = channel_->GetState(false);  // false = don't try to connect
    return state == GRPC_CHANNEL_READY;
}

bool OtlpMetricsClient::export_metrics(
    opentelemetry::proto::metrics::v1::ResourceMetrics& resource_metrics) {

    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceRequest request;
    *request.add_resource_metrics() = std::move(resource_metrics);

    opentelemetry::proto::collector::metrics::v1::ExportMetricsServiceResponse response;
    grpc::ClientContext context;
    context.set_deadline(std::chrono::system_clock::now() + config_.timeout);

    grpc::Status status = stub_->Export(&context, request, &response);

    if (!status.ok()) {
        // Only log on state transition to avoid spam
        if (was_connected_) {
            LOG(WARNING) << "Lost connection to OTLP endpoint: " << status.error_message();
            was_connected_ = false;
        }
        return false;
    }

    // Log on reconnection
    if (!was_connected_) {
        LOG(INFO) << "Connected to OTLP endpoint " << config_.endpoint;
        was_connected_ = true;
    }

    return true;
}

opentelemetry::proto::metrics::v1::ResourceMetrics
OtlpMetricsClient::create_resource_metrics() {
    opentelemetry::proto::metrics::v1::ResourceMetrics rm;

    auto* resource = rm.mutable_resource();

    // Service name (required)
    auto* service_name = resource->add_attributes();
    service_name->set_key("service.name");
    service_name->mutable_value()->set_string_value(config_.service_name);

    // Service namespace
    auto* service_ns = resource->add_attributes();
    service_ns->set_key("service.namespace");
    service_ns->mutable_value()->set_string_value(config_.service_namespace);

    // Service version
    auto* service_ver = resource->add_attributes();
    service_ver->set_key("service.version");
    service_ver->mutable_value()->set_string_value(config_.service_version);

    // Host name
    auto* host = resource->add_attributes();
    host->set_key("host.name");
    host->mutable_value()->set_string_value(config_.host_name);

    // Telemetry SDK
    auto* sdk_name = resource->add_attributes();
    sdk_name->set_key("telemetry.sdk.name");
    sdk_name->mutable_value()->set_string_value("vep-host-metrics");

    auto* sdk_lang = resource->add_attributes();
    sdk_lang->set_key("telemetry.sdk.language");
    sdk_lang->mutable_value()->set_string_value("cpp");

    return rm;
}

void OtlpMetricsClient::add_gauge(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope_metrics,
    const std::string& name,
    const std::string& description,
    const std::string& unit,
    double value,
    uint64_t timestamp_ns,
    const std::vector<std::pair<std::string, std::string>>& attributes) {

    auto* metric = scope_metrics->add_metrics();
    metric->set_name(name);
    metric->set_description(description);
    metric->set_unit(unit);

    auto* gauge = metric->mutable_gauge();
    auto* dp = gauge->add_data_points();
    dp->set_time_unix_nano(timestamp_ns);
    dp->set_as_double(value);

    for (const auto& [key, val] : attributes) {
        auto* attr = dp->add_attributes();
        attr->set_key(key);
        attr->mutable_value()->set_string_value(val);
    }
}

void OtlpMetricsClient::add_counter(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope_metrics,
    const std::string& name,
    const std::string& description,
    const std::string& unit,
    int64_t value,
    uint64_t timestamp_ns,
    const std::vector<std::pair<std::string, std::string>>& attributes) {

    auto* metric = scope_metrics->add_metrics();
    metric->set_name(name);
    metric->set_description(description);
    metric->set_unit(unit);

    auto* sum = metric->mutable_sum();
    sum->set_is_monotonic(true);
    sum->set_aggregation_temporality(
        opentelemetry::proto::metrics::v1::AGGREGATION_TEMPORALITY_CUMULATIVE);

    auto* dp = sum->add_data_points();
    dp->set_time_unix_nano(timestamp_ns);
    dp->set_as_int(value);

    for (const auto& [key, val] : attributes) {
        auto* attr = dp->add_attributes();
        attr->set_key(key);
        attr->mutable_value()->set_string_value(val);
    }
}

uint64_t OtlpMetricsClient::now_ns() {
    auto now = std::chrono::system_clock::now();
    auto duration = now.time_since_epoch();
    return std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count();
}

}  // namespace host_metrics
}  // namespace vep
