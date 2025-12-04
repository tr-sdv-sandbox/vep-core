// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

/// @file output_sink.hpp
/// @brief Abstract base class for VDR output sinks
///
/// Sinks receive telemetry data from VDR and forward it to external systems.
/// Uses types generated from this project's telemetry.idl.

#include "telemetry.h"
#include "vss_signal.h"

#include <cstdint>
#include <string>

namespace integration {

/// Statistics for output sinks
struct SinkStats {
    uint64_t messages_sent = 0;
    uint64_t messages_failed = 0;
    uint64_t bytes_sent = 0;
    int64_t last_send_timestamp_ns = 0;
};

/// Abstract base class for output sinks.
/// Implementations receive telemetry messages and forward them to external systems.
class OutputSink {
public:
    virtual ~OutputSink() = default;

    /// Start the sink (connect, allocate resources)
    virtual bool start() = 0;

    /// Stop the sink (disconnect, flush, release resources)
    virtual void stop() = 0;

    /// Flush any buffered data
    virtual void flush() {}

    /// @name Message sending
    /// @{

    /// Send a VSS signal
    virtual void send(const vss_Signal& msg) = 0;

    /// Send a vehicle event
    virtual void send(const telemetry_events_Event& msg) = 0;

    /// Send a gauge metric
    virtual void send(const telemetry_metrics_Gauge& msg) = 0;

    /// Send a counter metric
    virtual void send(const telemetry_metrics_Counter& msg) = 0;

    /// Send a histogram metric
    virtual void send(const telemetry_metrics_Histogram& msg) = 0;

    /// Send a log entry
    virtual void send(const telemetry_logs_LogEntry& msg) = 0;

    /// Send a scalar diagnostic measurement
    virtual void send(const telemetry_diagnostics_ScalarMeasurement& msg) = 0;

    /// Send a vector diagnostic measurement
    virtual void send(const telemetry_diagnostics_VectorMeasurement& msg) = 0;

    /// @}

    /// Check if sink is healthy and ready
    virtual bool healthy() const = 0;

    /// Get statistics
    virtual SinkStats stats() const = 0;

    /// Get sink name for logging
    virtual std::string name() const = 0;
};

}  // namespace integration
