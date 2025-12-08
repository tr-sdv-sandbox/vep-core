// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// VEP Host Metrics Collector
//
// Collects Linux host metrics and exports them via OTLP gRPC to an
// OpenTelemetry collector (e.g., vep_otel_probe on localhost:4317).
//
// Metrics follow OpenTelemetry semantic conventions for system metrics.

#include <atomic>
#include <chrono>
#include <csignal>
#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "proc_reader.hpp"
#include "otlp_metrics_client.hpp"

DEFINE_string(endpoint, "localhost:4317", "OTLP gRPC endpoint");
DEFINE_int32(interval, 10, "Collection interval in seconds");
DEFINE_string(service_name, "vep_host_metrics", "Service name for OTLP resource");
DEFINE_bool(cpu, true, "Collect CPU metrics");
DEFINE_bool(memory, true, "Collect memory metrics");
DEFINE_bool(disk, true, "Collect disk I/O metrics");
DEFINE_bool(network, true, "Collect network metrics");
DEFINE_bool(filesystem, true, "Collect filesystem usage metrics");

namespace {
std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

using namespace vep::host_metrics;

// Previous CPU times for calculating utilization
std::vector<CpuTimes> g_prev_cpu_times;

void collect_cpu_metrics(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope,
    uint64_t ts) {

    auto current = ProcReader::read_cpu_times();
    if (current.empty()) {
        return;
    }

    // On first call, just store times
    if (g_prev_cpu_times.empty()) {
        g_prev_cpu_times = current;
        return;
    }

    // Calculate utilization from deltas (aggregate CPU, index 0)
    const auto& prev = g_prev_cpu_times[0];
    const auto& curr = current[0];

    uint64_t total_delta = curr.total() - prev.total();
    if (total_delta == 0) {
        g_prev_cpu_times = current;
        return;
    }

    double scale = 100.0 / total_delta;

    // system.cpu.utilization (aggregate)
    double utilization = static_cast<double>(curr.active() - prev.active()) * scale / 100.0;
    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.utilization", "CPU utilization (0-1)", "1",
        utilization, ts);

    // Per-state breakdown (as ratios)
    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.time", "CPU time by state", "s",
        static_cast<double>(curr.user - prev.user) * scale / 100.0, ts,
        {{"state", "user"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.time", "CPU time by state", "s",
        static_cast<double>(curr.system - prev.system) * scale / 100.0, ts,
        {{"state", "system"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.time", "CPU time by state", "s",
        static_cast<double>(curr.idle - prev.idle) * scale / 100.0, ts,
        {{"state", "idle"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.time", "CPU time by state", "s",
        static_cast<double>(curr.iowait - prev.iowait) * scale / 100.0, ts,
        {{"state", "iowait"}});

    // CPU count
    OtlpMetricsClient::add_gauge(scope,
        "system.cpu.count", "Number of logical CPUs", "{cpus}",
        static_cast<double>(ProcReader::get_cpu_count()), ts);

    g_prev_cpu_times = current;
}

void collect_memory_metrics(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope,
    uint64_t ts) {

    auto mem = ProcReader::read_memory();
    if (mem.total == 0) {
        return;
    }

    // system.memory.usage
    OtlpMetricsClient::add_gauge(scope,
        "system.memory.usage", "Memory in use", "By",
        static_cast<double>(mem.used()), ts,
        {{"state", "used"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.memory.usage", "Memory free", "By",
        static_cast<double>(mem.free), ts,
        {{"state", "free"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.memory.usage", "Memory in buffers", "By",
        static_cast<double>(mem.buffers), ts,
        {{"state", "buffers"}});

    OtlpMetricsClient::add_gauge(scope,
        "system.memory.usage", "Memory in cache", "By",
        static_cast<double>(mem.cached), ts,
        {{"state", "cached"}});

    // system.memory.utilization
    double util = static_cast<double>(mem.used()) / static_cast<double>(mem.total);
    OtlpMetricsClient::add_gauge(scope,
        "system.memory.utilization", "Memory utilization (0-1)", "1",
        util, ts);

    // system.memory.limit (total)
    OtlpMetricsClient::add_gauge(scope,
        "system.memory.limit", "Total memory", "By",
        static_cast<double>(mem.total), ts);

    // Swap
    if (mem.swap_total > 0) {
        OtlpMetricsClient::add_gauge(scope,
            "system.paging.usage", "Swap used", "By",
            static_cast<double>(mem.swap_total - mem.swap_free), ts,
            {{"state", "used"}});

        OtlpMetricsClient::add_gauge(scope,
            "system.paging.usage", "Swap free", "By",
            static_cast<double>(mem.swap_free), ts,
            {{"state", "free"}});
    }
}

// Previous disk stats for calculating rates
std::map<std::string, DiskStats> g_prev_disk_stats;
std::chrono::steady_clock::time_point g_prev_disk_time;

void collect_disk_metrics(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope,
    uint64_t ts) {

    auto disks = ProcReader::read_disk_stats();
    auto now = std::chrono::steady_clock::now();

    for (const auto& disk : disks) {
        // Cumulative I/O operations
        OtlpMetricsClient::add_counter(scope,
            "system.disk.operations", "Disk read operations", "{operations}",
            static_cast<int64_t>(disk.reads_completed), ts,
            {{"device", disk.device}, {"direction", "read"}});

        OtlpMetricsClient::add_counter(scope,
            "system.disk.operations", "Disk write operations", "{operations}",
            static_cast<int64_t>(disk.writes_completed), ts,
            {{"device", disk.device}, {"direction", "write"}});

        // Cumulative bytes
        OtlpMetricsClient::add_counter(scope,
            "system.disk.io", "Disk bytes read", "By",
            static_cast<int64_t>(disk.bytes_read()), ts,
            {{"device", disk.device}, {"direction", "read"}});

        OtlpMetricsClient::add_counter(scope,
            "system.disk.io", "Disk bytes written", "By",
            static_cast<int64_t>(disk.bytes_written()), ts,
            {{"device", disk.device}, {"direction", "write"}});

        // Cumulative time
        OtlpMetricsClient::add_counter(scope,
            "system.disk.io_time", "Time spent on I/O", "s",
            static_cast<int64_t>(disk.io_time_ms / 1000), ts,
            {{"device", disk.device}});

        // Current pending I/O
        OtlpMetricsClient::add_gauge(scope,
            "system.disk.pending_operations", "Pending I/O operations", "{operations}",
            static_cast<double>(disk.io_in_progress), ts,
            {{"device", disk.device}});

        g_prev_disk_stats[disk.device] = disk;
    }

    g_prev_disk_time = now;
}

void collect_network_metrics(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope,
    uint64_t ts) {

    auto nets = ProcReader::read_network_stats();

    for (const auto& net : nets) {
        // Cumulative bytes
        OtlpMetricsClient::add_counter(scope,
            "system.network.io", "Network bytes received", "By",
            static_cast<int64_t>(net.rx_bytes), ts,
            {{"device", net.interface}, {"direction", "receive"}});

        OtlpMetricsClient::add_counter(scope,
            "system.network.io", "Network bytes transmitted", "By",
            static_cast<int64_t>(net.tx_bytes), ts,
            {{"device", net.interface}, {"direction", "transmit"}});

        // Cumulative packets
        OtlpMetricsClient::add_counter(scope,
            "system.network.packets", "Network packets received", "{packets}",
            static_cast<int64_t>(net.rx_packets), ts,
            {{"device", net.interface}, {"direction", "receive"}});

        OtlpMetricsClient::add_counter(scope,
            "system.network.packets", "Network packets transmitted", "{packets}",
            static_cast<int64_t>(net.tx_packets), ts,
            {{"device", net.interface}, {"direction", "transmit"}});

        // Errors
        OtlpMetricsClient::add_counter(scope,
            "system.network.errors", "Network receive errors", "{errors}",
            static_cast<int64_t>(net.rx_errors), ts,
            {{"device", net.interface}, {"direction", "receive"}});

        OtlpMetricsClient::add_counter(scope,
            "system.network.errors", "Network transmit errors", "{errors}",
            static_cast<int64_t>(net.tx_errors), ts,
            {{"device", net.interface}, {"direction", "transmit"}});

        // Dropped
        OtlpMetricsClient::add_counter(scope,
            "system.network.dropped", "Network packets dropped", "{packets}",
            static_cast<int64_t>(net.rx_dropped), ts,
            {{"device", net.interface}, {"direction", "receive"}});

        OtlpMetricsClient::add_counter(scope,
            "system.network.dropped", "Network packets dropped", "{packets}",
            static_cast<int64_t>(net.tx_dropped), ts,
            {{"device", net.interface}, {"direction", "transmit"}});
    }
}

void collect_filesystem_metrics(
    opentelemetry::proto::metrics::v1::ScopeMetrics* scope,
    uint64_t ts) {

    auto filesystems = ProcReader::read_filesystem_stats();

    for (const auto& fs : filesystems) {
        // Filesystem usage
        OtlpMetricsClient::add_gauge(scope,
            "system.filesystem.usage", "Filesystem bytes used", "By",
            static_cast<double>(fs.used_bytes), ts,
            {{"device", fs.device}, {"mountpoint", fs.mountpoint},
             {"type", fs.fstype}, {"state", "used"}});

        OtlpMetricsClient::add_gauge(scope,
            "system.filesystem.usage", "Filesystem bytes free", "By",
            static_cast<double>(fs.free_bytes), ts,
            {{"device", fs.device}, {"mountpoint", fs.mountpoint},
             {"type", fs.fstype}, {"state", "free"}});

        // Filesystem utilization
        if (fs.total_bytes > 0) {
            double util = static_cast<double>(fs.used_bytes) /
                          static_cast<double>(fs.total_bytes);
            OtlpMetricsClient::add_gauge(scope,
                "system.filesystem.utilization", "Filesystem utilization (0-1)", "1",
                util, ts,
                {{"device", fs.device}, {"mountpoint", fs.mountpoint},
                 {"type", fs.fstype}});
        }

        // Inode usage
        OtlpMetricsClient::add_gauge(scope,
            "system.filesystem.inodes.usage", "Inodes used", "{inodes}",
            static_cast<double>(fs.used_inodes), ts,
            {{"device", fs.device}, {"mountpoint", fs.mountpoint},
             {"type", fs.fstype}, {"state", "used"}});

        OtlpMetricsClient::add_gauge(scope,
            "system.filesystem.inodes.usage", "Inodes free", "{inodes}",
            static_cast<double>(fs.free_inodes), ts,
            {{"device", fs.device}, {"mountpoint", fs.mountpoint},
             {"type", fs.fstype}, {"state", "free"}});
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    gflags::SetUsageMessage(
        "VEP Host Metrics Collector\n\n"
        "Collects Linux host metrics and exports them via OTLP gRPC.\n\n"
        "Example:\n"
        "  vep_host_metrics --endpoint=localhost:4317 --interval=10");
    gflags::SetVersionString("0.1.0");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    LOG(INFO) << "VEP Host Metrics Collector starting";
    LOG(INFO) << "  Endpoint: " << FLAGS_endpoint;
    LOG(INFO) << "  Interval: " << FLAGS_interval << "s";
    LOG(INFO) << "  Metrics: "
              << (FLAGS_cpu ? "cpu " : "")
              << (FLAGS_memory ? "memory " : "")
              << (FLAGS_disk ? "disk " : "")
              << (FLAGS_network ? "network " : "")
              << (FLAGS_filesystem ? "filesystem" : "");

    // Create OTLP client
    OtlpConfig config;
    config.endpoint = FLAGS_endpoint;
    config.service_name = FLAGS_service_name;

    OtlpMetricsClient client(config);

    // Wait for OTLP endpoint to become available
    LOG(INFO) << "Waiting for OTLP endpoint...";
    while (g_running && !client.wait_for_connection(&g_running, 5)) {
        LOG(INFO) << "Retrying connection to " << FLAGS_endpoint << "...";
    }

    if (!g_running) {
        LOG(INFO) << "Interrupted while waiting for connection";
        return 0;
    }

    // Pre-populate CPU baseline
    if (FLAGS_cpu) {
        g_prev_cpu_times = ProcReader::read_cpu_times();
    }

    LOG(INFO) << "Starting collection loop...";

    while (g_running) {
        auto start = std::chrono::steady_clock::now();
        uint64_t ts = OtlpMetricsClient::now_ns();

        // Create resource metrics
        auto rm = client.create_resource_metrics();
        auto* scope = rm.add_scope_metrics();

        // Set scope (instrumentation library)
        auto* scope_info = scope->mutable_scope();
        scope_info->set_name("vep.host_metrics");
        scope_info->set_version("0.1.0");

        // Collect enabled metrics
        if (FLAGS_cpu) {
            collect_cpu_metrics(scope, ts);
        }
        if (FLAGS_memory) {
            collect_memory_metrics(scope, ts);
        }
        if (FLAGS_disk) {
            collect_disk_metrics(scope, ts);
        }
        if (FLAGS_network) {
            collect_network_metrics(scope, ts);
        }
        if (FLAGS_filesystem) {
            collect_filesystem_metrics(scope, ts);
        }

        // Export
        if (scope->metrics_size() > 0) {
            if (client.export_metrics(rm)) {
                VLOG(1) << "Exported " << scope->metrics_size() << " metrics";
            }
        }

        // Sleep for remainder of interval
        auto elapsed = std::chrono::steady_clock::now() - start;
        auto sleep_time = std::chrono::seconds(FLAGS_interval) - elapsed;
        if (sleep_time > std::chrono::milliseconds(0) && g_running) {
            std::this_thread::sleep_for(sleep_time);
        }
    }

    LOG(INFO) << "VEP Host Metrics Collector stopped";
    return 0;
}
