// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "unified_pipeline.hpp"

#include <glog/logging.h>

namespace vep::exporter {

UnifiedExporterPipeline::UnifiedExporterPipeline(
    std::unique_ptr<TransportSink> transport,
    std::unique_ptr<Compressor> compressor,
    const UnifiedPipelineConfig& config)
    : config_(config)
    , transport_(std::move(transport))
    , compressor_(std::move(compressor))
    , builder_(config.source_id, config.batch_max_items) {
}

UnifiedExporterPipeline::~UnifiedExporterPipeline() {
    stop();
}

bool UnifiedExporterPipeline::start() {
    if (running_) {
        return true;
    }

    if (!transport_->start()) {
        LOG(ERROR) << "UnifiedExporterPipeline: Failed to start transport";
        return false;
    }

    running_ = true;
    flush_thread_ = std::thread(&UnifiedExporterPipeline::flush_loop, this);

    LOG(INFO) << "UnifiedExporterPipeline started"
              << " (transport=" << transport_->name()
              << ", compression=" << compressor_->name()
              << ", max_items=" << config_.batch_max_items
              << ", timeout=" << config_.batch_timeout.count() << "ms)";
    return true;
}

void UnifiedExporterPipeline::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    flush_cv_.notify_all();

    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }

    // Final flush
    do_flush();

    transport_->stop();

    LOG(INFO) << "UnifiedExporterPipeline stopped. Stats:"
              << " items=" << stats_.items_total
              << " (signals=" << stats_.signals_processed
              << ", events=" << stats_.events_processed
              << ", metrics=" << stats_.metrics_processed
              << ", logs=" << stats_.logs_processed << ")"
              << " batches=" << stats_.batches_sent
              << " compression=" << (stats_.compression_ratio() * 100.0) << "%";
}

void UnifiedExporterPipeline::flush() {
    flush_cv_.notify_one();
}

void UnifiedExporterPipeline::flush_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(flush_mutex_);
        flush_cv_.wait_for(lock, config_.batch_timeout);

        if (!running_) break;

        lock.unlock();
        do_flush();
    }
}

void UnifiedExporterPipeline::do_flush() {
    if (!builder_.ready()) {
        return;
    }

    auto batch_data = builder_.build();
    if (batch_data.empty()) {
        return;
    }

    auto compressed = compressor_->compress(batch_data);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_before_compression += batch_data.size();
        stats_.bytes_after_compression += compressed.size();
        stats_.batches_sent++;
    }

    bool success = transport_->publish(config_.topic, compressed);
    if (!success) {
        LOG(WARNING) << "UnifiedExporterPipeline: Failed to publish batch";
    }
}

void UnifiedExporterPipeline::check_flush_needed() {
    // Flush if batch is full (by item count or size)
    if (builder_.full() || builder_.estimated_size() >= config_.batch_max_bytes) {
        flush_cv_.notify_one();
    }
}

void UnifiedExporterPipeline::send(const vep_VssSignal& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.signals_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

void UnifiedExporterPipeline::send(const vep_Event& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

void UnifiedExporterPipeline::send(const vep_OtelGauge& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

void UnifiedExporterPipeline::send(const vep_OtelCounter& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

void UnifiedExporterPipeline::send(const vep_OtelHistogram& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

void UnifiedExporterPipeline::send(const vep_OtelLogEntry& msg) {
    if (!running_) return;

    builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.logs_processed++;
        stats_.items_total++;
    }

    check_flush_needed();
}

bool UnifiedExporterPipeline::healthy() const {
    return running_ && transport_->healthy();
}

UnifiedPipelineStats UnifiedExporterPipeline::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace vep::exporter
