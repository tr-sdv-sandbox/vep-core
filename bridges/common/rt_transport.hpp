// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file rt_transport.hpp
/// @brief RT (Real-Time) transport interface for actuator commands
///
/// This defines the interface for sending actuator target values from
/// HPC (High-Performance Computer) to the RT (Real-Time) controller.
///
/// The RT side is the source of truth for all actuator logic.
/// HPC sends target requests, RT validates/arbitrates, and returns actual values.
///
/// For now, this is a stub that just logs commands. Future implementations:
/// - Raw Ethernet frames
/// - IEEE 1722 AVTP
/// - Shared memory (hypervisor)
/// - UDP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <variant>

namespace bridge {

/// Actuator value that can be sent to RT
using ActuatorValue = std::variant<
    bool,
    int8_t, int16_t, int32_t, int64_t,
    uint8_t, uint16_t, uint32_t, uint64_t,
    float, double,
    std::string
>;

/// Callback for receiving actual values from RT
using ActualValueCallback = std::function<void(
    const std::string& path,
    const ActuatorValue& actual_value
)>;

/// Interface for RT transport
class RtTransport {
public:
    virtual ~RtTransport() = default;

    /// Initialize the transport
    /// @return true on success
    virtual bool initialize() = 0;

    /// Shutdown the transport
    virtual void shutdown() = 0;

    /// Send actuator target request to RT
    /// @param path VSS path (e.g., "Vehicle.Cabin.Light.Intensity")
    /// @param target_value Requested target value
    /// @return true if sent successfully (not necessarily accepted by RT)
    virtual bool send_actuator_target(
        const std::string& path,
        const ActuatorValue& target_value
    ) = 0;

    /// Register callback for receiving actual values from RT
    /// @param callback Called when RT reports actual actuator values
    virtual void on_actual_value(ActualValueCallback callback) = 0;
};

/// Stub implementation that just logs actuator commands
/// Use this for testing when no real RT is available
class LoggingRtTransport : public RtTransport {
public:
    bool initialize() override;
    void shutdown() override;

    bool send_actuator_target(
        const std::string& path,
        const ActuatorValue& target_value
    ) override;

    void on_actual_value(ActualValueCallback callback) override;

    /// Simulate RT sending back an actual value (for testing)
    void simulate_actual_value(const std::string& path, const ActuatorValue& value);

protected:
    ActualValueCallback actual_callback_;
};

/// Loopback transport that echoes targets back as actuals
/// Use this to simulate successful actuation without real RT hardware
class LoopbackRtTransport : public RtTransport {
public:
    /// @param delay_ms Delay before echoing actual (simulates RT processing time)
    explicit LoopbackRtTransport(int delay_ms = 100);

    bool initialize() override;
    void shutdown() override;

    bool send_actuator_target(
        const std::string& path,
        const ActuatorValue& target_value
    ) override;

    void on_actual_value(ActualValueCallback callback) override;

private:
    int delay_ms_;
    ActualValueCallback actual_callback_;
    std::atomic<bool> running_{false};
};

}  // namespace bridge
