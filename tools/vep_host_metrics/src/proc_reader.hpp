// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Linux /proc filesystem reader for host metrics

#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace vep {
namespace host_metrics {

/// CPU time breakdown per logical CPU
struct CpuTimes {
    uint64_t user = 0;      // Time spent in user mode
    uint64_t nice = 0;      // Time spent in user mode with low priority
    uint64_t system = 0;    // Time spent in system mode
    uint64_t idle = 0;      // Time spent idle
    uint64_t iowait = 0;    // Time waiting for I/O
    uint64_t irq = 0;       // Time servicing interrupts
    uint64_t softirq = 0;   // Time servicing softirqs
    uint64_t steal = 0;     // Time stolen by hypervisor
    uint64_t guest = 0;     // Time running guest OS
    uint64_t guest_nice = 0;// Time running niced guest OS

    uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }

    uint64_t active() const {
        return total() - idle - iowait;
    }
};

/// Memory statistics in bytes
struct MemoryStats {
    uint64_t total = 0;
    uint64_t free = 0;
    uint64_t available = 0;
    uint64_t buffers = 0;
    uint64_t cached = 0;
    uint64_t swap_total = 0;
    uint64_t swap_free = 0;
    uint64_t slab = 0;
    uint64_t slab_reclaimable = 0;
    uint64_t slab_unreclaimable = 0;

    uint64_t used() const {
        return total - free - buffers - cached;
    }
};

/// Disk I/O statistics per device
struct DiskStats {
    std::string device;
    uint64_t reads_completed = 0;
    uint64_t reads_merged = 0;
    uint64_t sectors_read = 0;
    uint64_t read_time_ms = 0;
    uint64_t writes_completed = 0;
    uint64_t writes_merged = 0;
    uint64_t sectors_written = 0;
    uint64_t write_time_ms = 0;
    uint64_t io_in_progress = 0;
    uint64_t io_time_ms = 0;
    uint64_t weighted_io_time_ms = 0;

    // Sector size is typically 512 bytes
    static constexpr uint64_t SECTOR_SIZE = 512;

    uint64_t bytes_read() const { return sectors_read * SECTOR_SIZE; }
    uint64_t bytes_written() const { return sectors_written * SECTOR_SIZE; }
};

/// Network interface statistics
struct NetworkStats {
    std::string interface;
    uint64_t rx_bytes = 0;
    uint64_t rx_packets = 0;
    uint64_t rx_errors = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_bytes = 0;
    uint64_t tx_packets = 0;
    uint64_t tx_errors = 0;
    uint64_t tx_dropped = 0;
};

/// Filesystem usage statistics
struct FilesystemStats {
    std::string device;
    std::string mountpoint;
    std::string fstype;
    uint64_t total_bytes = 0;
    uint64_t used_bytes = 0;
    uint64_t free_bytes = 0;
    uint64_t total_inodes = 0;
    uint64_t used_inodes = 0;
    uint64_t free_inodes = 0;
};

/// System uptime
struct UptimeStats {
    double uptime_seconds = 0.0;
    double idle_seconds = 0.0;
};

/// Reader for Linux /proc filesystem
class ProcReader {
public:
    /// Read CPU times for all CPUs (index 0 = aggregate, 1+ = per-CPU)
    static std::vector<CpuTimes> read_cpu_times();

    /// Read memory statistics
    static MemoryStats read_memory();

    /// Read disk I/O statistics for all block devices
    static std::vector<DiskStats> read_disk_stats();

    /// Read network interface statistics
    static std::vector<NetworkStats> read_network_stats();

    /// Read filesystem usage (from /proc/mounts + statfs)
    static std::vector<FilesystemStats> read_filesystem_stats();

    /// Read system uptime
    static UptimeStats read_uptime();

    /// Get number of logical CPUs
    static int get_cpu_count();

    /// Get hostname
    static std::string get_hostname();
};

}  // namespace host_metrics
}  // namespace vep
