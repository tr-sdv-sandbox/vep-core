// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rt_transport.hpp"

#include <glog/logging.h>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace bridge {

namespace {

// Helper to convert ActuatorValue to string for logging
std::string value_to_string(const ActuatorValue& value) {
    return std::visit([](auto&& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, bool>) {
            return v ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return "\"" + v + "\"";
        } else if constexpr (std::is_integral_v<T>) {
            return std::to_string(static_cast<int64_t>(v));
        } else if constexpr (std::is_floating_point_v<T>) {
            return std::to_string(v);
        } else {
            return "<unknown>";
        }
    }, value);
}

}  // namespace

// =============================================================================
// LoggingRtTransport implementation
// =============================================================================

bool LoggingRtTransport::initialize() {
    LOG(INFO) << "[RT TRANSPORT] Initialized (logging stub)";
    LOG(INFO) << "[RT TRANSPORT] Actuator commands will be logged but not executed";
    return true;
}

void LoggingRtTransport::shutdown() {
    LOG(INFO) << "[RT TRANSPORT] Shutdown";
}

bool LoggingRtTransport::send_actuator_target(
    const std::string& path,
    const ActuatorValue& target_value
) {
    LOG(INFO) << "[RT TRANSPORT] Actuator target request:"
              << " path=" << path
              << " target=" << value_to_string(target_value);

    // In a real implementation, this would:
    // 1. Serialize the message
    // 2. Send via AVTP, raw ethernet, shared memory, etc.
    // 3. Return false if send fails

    return true;
}

void LoggingRtTransport::on_actual_value(ActualValueCallback callback) {
    actual_callback_ = std::move(callback);
}

void LoggingRtTransport::simulate_actual_value(
    const std::string& path,
    const ActuatorValue& value
) {
    LOG(INFO) << "[RT TRANSPORT] Simulating actual value:"
              << " path=" << path
              << " actual=" << value_to_string(value);

    if (actual_callback_) {
        actual_callback_(path, value);
    }
}

// =============================================================================
// LoopbackRtTransport implementation
// =============================================================================

LoopbackRtTransport::LoopbackRtTransport(int delay_ms)
    : delay_ms_(delay_ms) {
}

bool LoopbackRtTransport::initialize() {
    running_ = true;
    LOG(INFO) << "[RT LOOPBACK] Initialized with " << delay_ms_ << "ms delay";
    LOG(INFO) << "[RT LOOPBACK] All actuator targets will be echoed back as actuals";
    return true;
}

void LoopbackRtTransport::shutdown() {
    running_ = false;
    LOG(INFO) << "[RT LOOPBACK] Shutdown";
}

bool LoopbackRtTransport::send_actuator_target(
    const std::string& path,
    const ActuatorValue& target_value
) {
    if (!running_) {
        return false;
    }

    LOG(INFO) << "[RT LOOPBACK] Received target:"
              << " path=" << path
              << " value=" << value_to_string(target_value);

    // Spawn a thread to echo back after delay (simulates RT processing)
    std::thread([this, path, target_value]() {
        if (delay_ms_ > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        }

        if (running_ && actual_callback_) {
            LOG(INFO) << "[RT LOOPBACK] Echoing actual:"
                      << " path=" << path
                      << " value=" << value_to_string(target_value);
            actual_callback_(path, target_value);
        }
    }).detach();

    return true;
}

void LoopbackRtTransport::on_actual_value(ActualValueCallback callback) {
    actual_callback_ = std::move(callback);
}

// =============================================================================
// UdpRtTransport implementation
// =============================================================================

UdpRtTransport::UdpRtTransport(const std::string& target_host, uint16_t target_port,
                               uint16_t listen_port, const std::string& multicast_interface)
    : target_host_(target_host)
    , target_port_(target_port)
    , listen_port_(listen_port)
    , multicast_interface_(multicast_interface) {
}

bool UdpRtTransport::is_multicast_address(const std::string& addr) {
    struct in_addr in;
    if (inet_pton(AF_INET, addr.c_str(), &in) != 1) {
        return false;
    }
    // Multicast range: 224.0.0.0 - 239.255.255.255
    uint32_t ip = ntohl(in.s_addr);
    return (ip >= 0xE0000000 && ip <= 0xEFFFFFFF);
}

bool UdpRtTransport::setup_multicast_socket() {
    // Enable multicast loopback (so we can receive our own messages if needed)
    int loopback = 1;
    if (setsockopt(send_socket_, IPPROTO_IP, IP_MULTICAST_LOOP, &loopback, sizeof(loopback)) < 0) {
        LOG(WARNING) << "[RT UDP] Failed to set multicast loopback: " << strerror(errno);
    }

    // Set TTL for multicast (default 1 = local network only)
    int ttl = 1;
    if (setsockopt(send_socket_, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0) {
        LOG(WARNING) << "[RT UDP] Failed to set multicast TTL: " << strerror(errno);
    }

    // Set outgoing interface for multicast if specified
    if (!multicast_interface_.empty()) {
        struct ip_mreqn mreq;
        memset(&mreq, 0, sizeof(mreq));
        mreq.imr_ifindex = if_nametoindex(multicast_interface_.c_str());
        if (mreq.imr_ifindex == 0) {
            LOG(ERROR) << "[RT UDP] Invalid multicast interface: " << multicast_interface_;
            return false;
        }
        if (setsockopt(send_socket_, IPPROTO_IP, IP_MULTICAST_IF, &mreq, sizeof(mreq)) < 0) {
            LOG(ERROR) << "[RT UDP] Failed to set multicast interface: " << strerror(errno);
            return false;
        }
        LOG(INFO) << "[RT UDP] Multicast interface set to: " << multicast_interface_;
    }

    return true;
}

UdpRtTransport::~UdpRtTransport() {
    shutdown();
}

bool UdpRtTransport::initialize() {
    // Check if target is multicast
    is_multicast_ = is_multicast_address(target_host_);

    // Create send socket
    send_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (send_socket_ < 0) {
        LOG(ERROR) << "[RT UDP] Failed to create send socket: " << strerror(errno);
        return false;
    }

    // Configure multicast if needed
    if (is_multicast_) {
        LOG(INFO) << "[RT UDP] Target is multicast address: " << target_host_;
        if (!setup_multicast_socket()) {
            close(send_socket_);
            send_socket_ = -1;
            return false;
        }
    }

    // Create receive socket if listen_port is specified
    if (listen_port_ > 0) {
        recv_socket_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (recv_socket_ < 0) {
            LOG(ERROR) << "[RT UDP] Failed to create recv socket: " << strerror(errno);
            close(send_socket_);
            send_socket_ = -1;
            return false;
        }

        // Bind receive socket
        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(listen_port_);

        if (bind(recv_socket_, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            LOG(ERROR) << "[RT UDP] Failed to bind recv socket: " << strerror(errno);
            close(send_socket_);
            close(recv_socket_);
            send_socket_ = -1;
            recv_socket_ = -1;
            return false;
        }

        // Start receive thread
        running_ = true;
        recv_thread_ = std::thread(&UdpRtTransport::recv_loop, this);
    }

    LOG(INFO) << "[RT UDP] Initialized: target=" << target_host_ << ":" << target_port_
              << (is_multicast_ ? " (multicast)" : " (unicast)");
    if (listen_port_ > 0) {
        LOG(INFO) << "[RT UDP] Listening for actuals on port " << listen_port_;
    }

    return true;
}

void UdpRtTransport::shutdown() {
    running_ = false;

    if (recv_socket_ >= 0) {
        ::shutdown(recv_socket_, SHUT_RDWR);
        close(recv_socket_);
        recv_socket_ = -1;
    }

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    if (send_socket_ >= 0) {
        close(send_socket_);
        send_socket_ = -1;
    }

    LOG(INFO) << "[RT UDP] Shutdown";
}

bool UdpRtTransport::send_actuator_target(
    const std::string& path,
    const ActuatorValue& target_value
) {
    if (send_socket_ < 0) {
        LOG(ERROR) << "[RT UDP] Socket not initialized";
        return false;
    }

    // Get current timestamp
    auto now = std::chrono::system_clock::now();
    auto timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        now.time_since_epoch()).count();

    // Format message: PATH|VALUE|TIMESTAMP_NS
    std::string message = path + "|" + value_to_string(target_value) + "|" + std::to_string(timestamp_ns);

    // Setup destination address
    struct sockaddr_in dest_addr;
    memset(&dest_addr, 0, sizeof(dest_addr));
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(target_port_);

    if (inet_pton(AF_INET, target_host_.c_str(), &dest_addr.sin_addr) <= 0) {
        LOG(ERROR) << "[RT UDP] Invalid target address: " << target_host_;
        return false;
    }

    // Send the message
    ssize_t sent = sendto(send_socket_, message.c_str(), message.size(), 0,
                          (struct sockaddr*)&dest_addr, sizeof(dest_addr));

    if (sent < 0) {
        LOG(ERROR) << "[RT UDP] Failed to send: " << strerror(errno);
        return false;
    }

    LOG(INFO) << "[RT UDP] Sent to " << target_host_ << ":" << target_port_
              << " path=" << path << " value=" << value_to_string(target_value);

    return true;
}

void UdpRtTransport::on_actual_value(ActualValueCallback callback) {
    actual_callback_ = std::move(callback);
}

void UdpRtTransport::recv_loop() {
    LOG(INFO) << "[RT UDP] Receive thread started";

    char buffer[4096];

    while (running_) {
        struct sockaddr_in sender_addr;
        socklen_t sender_len = sizeof(sender_addr);

        ssize_t received = recvfrom(recv_socket_, buffer, sizeof(buffer) - 1, 0,
                                    (struct sockaddr*)&sender_addr, &sender_len);

        if (received < 0) {
            if (running_) {
                LOG(ERROR) << "[RT UDP] Receive error: " << strerror(errno);
            }
            continue;
        }

        buffer[received] = '\0';
        std::string message(buffer, received);

        // Parse message: PATH|VALUE|TIMESTAMP_NS
        size_t first_pipe = message.find('|');
        size_t second_pipe = message.find('|', first_pipe + 1);

        if (first_pipe == std::string::npos || second_pipe == std::string::npos) {
            LOG(WARNING) << "[RT UDP] Invalid message format: " << message;
            continue;
        }

        std::string path = message.substr(0, first_pipe);
        std::string value_str = message.substr(first_pipe + 1, second_pipe - first_pipe - 1);

        LOG(INFO) << "[RT UDP] Received actual: path=" << path << " value=" << value_str;

        // Try to parse value as different types
        ActuatorValue value;
        if (value_str == "true") {
            value = true;
        } else if (value_str == "false") {
            value = false;
        } else if (value_str.find('.') != std::string::npos) {
            // Floating point
            try {
                value = std::stod(value_str);
            } catch (...) {
                value = value_str;
            }
        } else {
            // Try integer
            try {
                value = static_cast<int64_t>(std::stoll(value_str));
            } catch (...) {
                value = value_str;
            }
        }

        if (actual_callback_) {
            actual_callback_(path, value);
        }
    }

    LOG(INFO) << "[RT UDP] Receive thread stopped";
}

}  // namespace bridge
