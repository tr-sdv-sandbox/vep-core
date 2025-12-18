// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file exporter_pipeline.hpp
/// @brief Modular exporter pipeline using composable components
///
/// ExporterPipeline orchestrates:
/// - Batch building (SignalBatchBuilder, EventBatchBuilder, etc.)
/// - Compression (Compressor interface)
/// - Transport (TransportSink interface)
///
/// This replaces the monolithic CompressedMqttSink with a composable design.

#include "batch_builder.hpp"
#include "compressor.hpp"
#include "transport_sink.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vep::exporter {

/// Configuration for the exporter pipeline
struct PipelineConfig {
    std::string source_id = "vdr";

    // Batching thresholds
    size_t batch_max_signals = 100;
    size_t batch_max_events = 50;
    size_t batch_max_metrics = 100;
    size_t batch_max_logs = 100;

    // Flush timeout
    std::chrono::milliseconds batch_timeout{1000};

    // Topic names
    std::string topic_signals = "signals";
    std::string topic_events = "events";
    std::string topic_metrics = "metrics";
    std::string topic_logs = "logs";
};

/// Statistics for the exporter pipeline
struct PipelineStats {
    uint64_t signals_processed = 0;
    uint64_t events_processed = 0;
    uint64_t metrics_processed = 0;
    uint64_t logs_processed = 0;
    uint64_t batches_sent = 0;
    uint64_t bytes_before_compression = 0;
    uint64_t bytes_after_compression = 0;

    double compression_ratio() const {
        return bytes_before_compression > 0
            ? static_cast<double>(bytes_after_compression) / bytes_before_compression
            : 0.0;
    }
};

/// Modular exporter pipeline
///
/// Composes BatchBuilders, Compressor, and TransportSink into a complete
/// export pipeline. Thread-safe for concurrent message ingestion.
class ExporterPipeline {
public:
    /// Create a pipeline with the given components
    /// @param transport Transport sink (MQTT, SOME/IP, etc.)
    /// @param compressor Compression strategy (zstd, none)
    /// @param config Pipeline configuration
    ExporterPipeline(
        std::unique_ptr<TransportSink> transport,
        std::unique_ptr<Compressor> compressor,
        const PipelineConfig& config = {});

    ~ExporterPipeline();

    ExporterPipeline(const ExporterPipeline&) = delete;
    ExporterPipeline& operator=(const ExporterPipeline&) = delete;

    /// Start the pipeline (starts transport and flush thread)
    bool start();

    /// Stop the pipeline (flushes remaining data)
    void stop();

    /// Flush all pending batches immediately
    void flush();

    /// @name Message ingestion
    /// Thread-safe methods to add messages to the pipeline
    /// @{
    void send(const vep_VssSignal& msg);
    void send(const vep_Event& msg);
    void send(const vep_OtelGauge& msg);
    void send(const vep_OtelCounter& msg);
    void send(const vep_OtelHistogram& msg);
    void send(const vep_OtelLogEntry& msg);
    /// @}

    /// Check if pipeline is healthy
    bool healthy() const;

    /// Get pipeline statistics
    PipelineStats stats() const;

private:
    void flush_loop();
    void flush_signals();
    void flush_events();
    void flush_metrics();
    void flush_logs();

    void send_batch(const std::string& topic, const std::vector<uint8_t>& data);

    PipelineConfig config_;

    // Components
    std::unique_ptr<TransportSink> transport_;
    std::unique_ptr<Compressor> compressor_;

    // Batch builders
    SignalBatchBuilder signal_builder_;
    EventBatchBuilder event_builder_;
    MetricsBatchBuilder metrics_builder_;
    LogBatchBuilder log_builder_;

    // State
    std::atomic<bool> running_{false};

    // Flush thread
    std::thread flush_thread_;
    std::condition_variable flush_cv_;
    std::mutex flush_mutex_;

    // Stats
    mutable std::mutex stats_mutex_;
    PipelineStats stats_;
};

}  // namespace vep::exporter
