// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "proc_reader.hpp"

#include <fstream>
#include <sstream>
#include <unistd.h>
#include <sys/statvfs.h>
#include <sys/sysinfo.h>

namespace vep {
namespace host_metrics {

std::vector<CpuTimes> ProcReader::read_cpu_times() {
    std::vector<CpuTimes> result;
    std::ifstream file("/proc/stat");
    if (!file.is_open()) {
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        if (line.substr(0, 3) != "cpu") {
            continue;
        }

        CpuTimes times;
        std::istringstream iss(line);
        std::string cpu_label;
        iss >> cpu_label;

        // Skip if not cpu or cpu<N>
        if (cpu_label != "cpu" && cpu_label.substr(0, 3) != "cpu") {
            continue;
        }

        iss >> times.user >> times.nice >> times.system >> times.idle
            >> times.iowait >> times.irq >> times.softirq >> times.steal
            >> times.guest >> times.guest_nice;

        result.push_back(times);

        // Stop after aggregate + all per-CPU entries
        if (cpu_label != "cpu" && result.size() > 1) {
            // We have at least one per-CPU entry, keep reading
        }
    }

    return result;
}

MemoryStats ProcReader::read_memory() {
    MemoryStats stats;
    std::ifstream file("/proc/meminfo");
    if (!file.is_open()) {
        return stats;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::string key;
        uint64_t value;
        std::string unit;

        std::istringstream iss(line);
        iss >> key >> value >> unit;

        // Remove trailing colon from key
        if (!key.empty() && key.back() == ':') {
            key.pop_back();
        }

        // Values in /proc/meminfo are in kB
        value *= 1024;

        if (key == "MemTotal") stats.total = value;
        else if (key == "MemFree") stats.free = value;
        else if (key == "MemAvailable") stats.available = value;
        else if (key == "Buffers") stats.buffers = value;
        else if (key == "Cached") stats.cached = value;
        else if (key == "SwapTotal") stats.swap_total = value;
        else if (key == "SwapFree") stats.swap_free = value;
        else if (key == "Slab") stats.slab = value;
        else if (key == "SReclaimable") stats.slab_reclaimable = value;
        else if (key == "SUnreclaim") stats.slab_unreclaimable = value;
    }

    return stats;
}

std::vector<DiskStats> ProcReader::read_disk_stats() {
    std::vector<DiskStats> result;
    std::ifstream file("/proc/diskstats");
    if (!file.is_open()) {
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);

        int major, minor;
        std::string device;
        iss >> major >> minor >> device;

        // Skip partitions (typically have numbers at end like sda1)
        // Only include whole disks and important virtual devices
        bool is_partition = false;
        if (!device.empty() && std::isdigit(device.back())) {
            // Check if it's a disk like nvme0n1 (not a partition)
            if (device.find("nvme") != 0 || device.find("p") != std::string::npos) {
                is_partition = true;
            }
        }

        // Skip loop devices and dm devices unless specifically wanted
        if (device.substr(0, 4) == "loop" || device.substr(0, 2) == "dm") {
            continue;
        }

        if (is_partition) {
            continue;
        }

        DiskStats stats;
        stats.device = device;

        iss >> stats.reads_completed >> stats.reads_merged >> stats.sectors_read
            >> stats.read_time_ms >> stats.writes_completed >> stats.writes_merged
            >> stats.sectors_written >> stats.write_time_ms >> stats.io_in_progress
            >> stats.io_time_ms >> stats.weighted_io_time_ms;

        // Only include devices with actual I/O
        if (stats.reads_completed > 0 || stats.writes_completed > 0) {
            result.push_back(stats);
        }
    }

    return result;
}

std::vector<NetworkStats> ProcReader::read_network_stats() {
    std::vector<NetworkStats> result;
    std::ifstream file("/proc/net/dev");
    if (!file.is_open()) {
        return result;
    }

    std::string line;
    int line_num = 0;
    while (std::getline(file, line)) {
        line_num++;
        // Skip header lines
        if (line_num <= 2) {
            continue;
        }

        // Format: "iface: rx_bytes rx_packets ... tx_bytes tx_packets ..."
        auto colon_pos = line.find(':');
        if (colon_pos == std::string::npos) {
            continue;
        }

        NetworkStats stats;
        stats.interface = line.substr(0, colon_pos);

        // Trim whitespace from interface name
        auto start = stats.interface.find_first_not_of(" \t");
        if (start != std::string::npos) {
            stats.interface = stats.interface.substr(start);
        }

        // Skip loopback
        if (stats.interface == "lo") {
            continue;
        }

        std::istringstream iss(line.substr(colon_pos + 1));

        // RX: bytes packets errs drop fifo frame compressed multicast
        uint64_t dummy;
        iss >> stats.rx_bytes >> stats.rx_packets >> stats.rx_errors >> stats.rx_dropped
            >> dummy >> dummy >> dummy >> dummy;

        // TX: bytes packets errs drop fifo colls carrier compressed
        iss >> stats.tx_bytes >> stats.tx_packets >> stats.tx_errors >> stats.tx_dropped;

        result.push_back(stats);
    }

    return result;
}

std::vector<FilesystemStats> ProcReader::read_filesystem_stats() {
    std::vector<FilesystemStats> result;
    std::ifstream file("/proc/mounts");
    if (!file.is_open()) {
        return result;
    }

    std::string line;
    while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string device, mountpoint, fstype;
        iss >> device >> mountpoint >> fstype;

        // Only include real filesystems
        if (fstype == "sysfs" || fstype == "proc" || fstype == "devtmpfs" ||
            fstype == "devpts" || fstype == "tmpfs" || fstype == "securityfs" ||
            fstype == "cgroup" || fstype == "cgroup2" || fstype == "pstore" ||
            fstype == "debugfs" || fstype == "tracefs" || fstype == "hugetlbfs" ||
            fstype == "mqueue" || fstype == "fusectl" || fstype == "configfs" ||
            fstype == "binfmt_misc" || fstype == "autofs" || fstype == "overlay" ||
            fstype == "squashfs" || fstype == "nsfs") {
            continue;
        }

        struct statvfs vfs;
        if (statvfs(mountpoint.c_str(), &vfs) != 0) {
            continue;
        }

        FilesystemStats stats;
        stats.device = device;
        stats.mountpoint = mountpoint;
        stats.fstype = fstype;
        stats.total_bytes = vfs.f_blocks * vfs.f_frsize;
        stats.free_bytes = vfs.f_bfree * vfs.f_frsize;
        stats.used_bytes = stats.total_bytes - stats.free_bytes;
        stats.total_inodes = vfs.f_files;
        stats.free_inodes = vfs.f_ffree;
        stats.used_inodes = stats.total_inodes - stats.free_inodes;

        // Skip pseudo-filesystems with 0 size
        if (stats.total_bytes > 0) {
            result.push_back(stats);
        }
    }

    return result;
}

UptimeStats ProcReader::read_uptime() {
    UptimeStats stats;
    std::ifstream file("/proc/uptime");
    if (!file.is_open()) {
        return stats;
    }

    file >> stats.uptime_seconds >> stats.idle_seconds;
    return stats;
}

int ProcReader::get_cpu_count() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

std::string ProcReader::get_hostname() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        return std::string(hostname);
    }
    return "unknown";
}

}  // namespace host_metrics
}  // namespace vep
