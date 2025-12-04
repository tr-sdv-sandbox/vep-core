// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "rt_transport.hpp"

#include <glog/logging.h>
#include <thread>
#include <chrono>

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

}  // namespace bridge
