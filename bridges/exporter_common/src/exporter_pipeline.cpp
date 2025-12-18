// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "exporter_pipeline.hpp"

#include <glog/logging.h>

namespace vep::exporter {

ExporterPipeline::ExporterPipeline(
    std::unique_ptr<TransportSink> transport,
    std::unique_ptr<Compressor> compressor,
    const PipelineConfig& config)
    : config_(config)
    , transport_(std::move(transport))
    , compressor_(std::move(compressor))
    , signal_builder_(config.source_id)
    , event_builder_(config.source_id)
    , metrics_builder_(config.source_id)
    , log_builder_(config.source_id) {
}

ExporterPipeline::~ExporterPipeline() {
    stop();
}

bool ExporterPipeline::start() {
    if (running_) {
        return true;
    }

    if (!transport_->start()) {
        LOG(ERROR) << "Failed to start transport";
        return false;
    }

    running_ = true;
    flush_thread_ = std::thread(&ExporterPipeline::flush_loop, this);

    LOG(INFO) << "ExporterPipeline started with " << transport_->name()
              << " transport and " << compressor_->name() << " compression";
    return true;
}

void ExporterPipeline::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    flush_cv_.notify_all();

    if (flush_thread_.joinable()) {
        flush_thread_.join();
    }

    // Final flush
    flush();

    transport_->stop();

    LOG(INFO) << "ExporterPipeline stopped. Stats: signals=" << stats_.signals_processed
              << " events=" << stats_.events_processed
              << " metrics=" << stats_.metrics_processed
              << " logs=" << stats_.logs_processed
              << " batches=" << stats_.batches_sent
              << " compression=" << (stats_.compression_ratio() * 100.0) << "%";
}

void ExporterPipeline::flush() {
    flush_signals();
    flush_events();
    flush_metrics();
    flush_logs();
}

void ExporterPipeline::flush_loop() {
    while (running_) {
        std::unique_lock<std::mutex> lock(flush_mutex_);
        flush_cv_.wait_for(lock, config_.batch_timeout);

        if (!running_) break;

        lock.unlock();
        flush();
    }
}

void ExporterPipeline::send(const vep_VssSignal& msg) {
    if (!running_) return;

    signal_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.signals_processed++;
    }

    if (signal_builder_.size() >= config_.batch_max_signals) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::send(const vep_Event& msg) {
    if (!running_) return;

    event_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.events_processed++;
    }

    if (event_builder_.size() >= config_.batch_max_events) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::send(const vep_OtelGauge& msg) {
    if (!running_) return;

    metrics_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
    }

    if (metrics_builder_.size() >= config_.batch_max_metrics) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::send(const vep_OtelCounter& msg) {
    if (!running_) return;

    metrics_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
    }

    if (metrics_builder_.size() >= config_.batch_max_metrics) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::send(const vep_OtelHistogram& msg) {
    if (!running_) return;

    metrics_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.metrics_processed++;
    }

    if (metrics_builder_.size() >= config_.batch_max_metrics) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::send(const vep_OtelLogEntry& msg) {
    if (!running_) return;

    log_builder_.add(msg);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.logs_processed++;
    }

    if (log_builder_.size() >= config_.batch_max_logs) {
        flush_cv_.notify_one();
    }
}

void ExporterPipeline::flush_signals() {
    if (!signal_builder_.ready()) return;

    auto batch = signal_builder_.build();
    send_batch(config_.topic_signals, batch);
}

void ExporterPipeline::flush_events() {
    if (!event_builder_.ready()) return;

    auto batch = event_builder_.build();
    send_batch(config_.topic_events, batch);
}

void ExporterPipeline::flush_metrics() {
    if (!metrics_builder_.ready()) return;

    auto batch = metrics_builder_.build();
    send_batch(config_.topic_metrics, batch);
}

void ExporterPipeline::flush_logs() {
    if (!log_builder_.ready()) return;

    auto batch = log_builder_.build();
    send_batch(config_.topic_logs, batch);
}

void ExporterPipeline::send_batch(const std::string& topic, const std::vector<uint8_t>& data) {
    if (data.empty()) return;

    auto compressed = compressor_->compress(data);

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_before_compression += data.size();
        stats_.bytes_after_compression += compressed.size();
        stats_.batches_sent++;
    }

    transport_->publish(topic, compressed);
}

bool ExporterPipeline::healthy() const {
    return running_ && transport_->healthy();
}

PipelineStats ExporterPipeline::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

}  // namespace vep::exporter
