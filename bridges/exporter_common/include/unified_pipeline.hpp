// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file unified_pipeline.hpp
/// @brief Unified exporter pipeline with interleaved batching
///
/// UnifiedExporterPipeline collects all data types (signals, events, metrics,
/// logs) into a single TransferBatch with items interleaved in arrival order.
///
/// This is simpler than ExporterPipeline (which uses 4 separate batches) and
/// is designed for transports where each application has a single content_id
/// (e.g., SOME/IP with BE Message Proxy).
///
/// Data flow:
///   DDS messages → UnifiedBatchBuilder → compress → BackendTransport

#include "batch_builder.hpp"
#include "compressor.hpp"
#include "vep/backend_transport.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace vep::exporter {

/// Configuration for the unified exporter pipeline
struct UnifiedPipelineConfig {
    std::string source_id = "vep_exporter";

    // Maximum items per batch (all types combined)
    size_t batch_max_items = 100;

    // Maximum batch size in bytes (approximate, before compression)
    size_t batch_max_bytes = 64 * 1024;  // 64 KB

    // Flush timeout - send batch even if not full
    std::chrono::milliseconds batch_timeout{1000};

    // Note: content_id is now configured in the transport, not in the pipeline
};

/// Statistics for the unified exporter pipeline
struct UnifiedPipelineStats {
    uint64_t signals_processed = 0;
    uint64_t events_processed = 0;
    uint64_t metrics_processed = 0;
    uint64_t logs_processed = 0;
    uint64_t items_total = 0;
    uint64_t batches_sent = 0;
    uint64_t bytes_before_compression = 0;
    uint64_t bytes_after_compression = 0;

    double compression_ratio() const {
        return bytes_before_compression > 0
            ? static_cast<double>(bytes_after_compression) / bytes_before_compression
            : 0.0;
    }
};

/// Unified exporter pipeline with interleaved batching
///
/// Collects all data types into a single TransferBatch, compresses,
/// and sends via the configured transport.
///
/// Thread-safe for concurrent message ingestion from multiple DDS callbacks.
///
/// Example:
/// @code
///   auto transport = std::make_unique<SomeipSink>(config);
///   auto compressor = create_compressor(CompressorType::ZSTD, 3);
///   UnifiedExporterPipeline pipeline(std::move(transport), std::move(compressor));
///
///   pipeline.start();
///   // In DDS callbacks:
///   pipeline.send(signal);
///   pipeline.send(event);
///   pipeline.send(gauge);
/// @endcode
class UnifiedExporterPipeline {
public:
    /// Create a pipeline with the given components
    /// @param transport Backend transport (MQTT, SOME/IP, etc.)
    /// @param compressor Compression strategy (zstd, none)
    /// @param config Pipeline configuration
    UnifiedExporterPipeline(
        std::unique_ptr<vep::BackendTransport> transport,
        std::unique_ptr<Compressor> compressor,
        const UnifiedPipelineConfig& config = {});

    ~UnifiedExporterPipeline();

    UnifiedExporterPipeline(const UnifiedExporterPipeline&) = delete;
    UnifiedExporterPipeline& operator=(const UnifiedExporterPipeline&) = delete;

    /// Start the pipeline (starts transport and flush thread)
    bool start();

    /// Stop the pipeline (flushes remaining data)
    void stop();

    /// Flush pending batch immediately
    void flush();

    /// @name Message ingestion
    /// Thread-safe methods to add messages to the pipeline.
    /// All types go into the same batch, interleaved in arrival order.
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
    UnifiedPipelineStats stats() const;

private:
    void flush_loop();
    void do_flush();
    void check_flush_needed();

    UnifiedPipelineConfig config_;

    // Components
    std::unique_ptr<vep::BackendTransport> transport_;
    std::unique_ptr<Compressor> compressor_;

    // Single unified batch builder
    UnifiedBatchBuilder builder_;

    // State
    std::atomic<bool> running_{false};

    // Flush thread
    std::thread flush_thread_;
    std::condition_variable flush_cv_;
    std::mutex flush_mutex_;

    // Stats
    mutable std::mutex stats_mutex_;
    UnifiedPipelineStats stats_;
};

}  // namespace vep::exporter
