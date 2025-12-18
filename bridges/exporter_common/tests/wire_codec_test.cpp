// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file wire_codec_test.cpp
/// @brief Round-trip tests for wire encoder/decoder
///
/// These tests verify that:
/// 1. DDS types can be encoded to protobuf
/// 2. Protobuf can be decoded to C++ types
/// 3. The decoded values match the original input

#include "batch_builder.hpp"
#include "wire_decoder.hpp"
#include "wire_encoder.hpp"

#include <gtest/gtest.h>
#include <cstring>

namespace vep::exporter::test {

// =============================================================================
// Test Helpers
// =============================================================================

vep_VssSignal create_double_signal(const char* path, double value, int64_t timestamp_ns) {
    vep_VssSignal signal = {};
    signal.path = const_cast<char*>(path);
    signal.header.source_id = const_cast<char*>("test");
    signal.header.timestamp_ns = timestamp_ns;
    signal.header.seq_num = 0;
    signal.header.correlation_id = const_cast<char*>("");
    signal.quality = vep_VSS_QUALITY_VALID;
    signal.value.type = vep_VSS_VALUE_TYPE_DOUBLE;
    signal.value.double_value = value;
    return signal;
}

vep_VssSignal create_int32_signal(const char* path, int32_t value, int64_t timestamp_ns) {
    vep_VssSignal signal = {};
    signal.path = const_cast<char*>(path);
    signal.header.source_id = const_cast<char*>("test");
    signal.header.timestamp_ns = timestamp_ns;
    signal.header.seq_num = 0;
    signal.header.correlation_id = const_cast<char*>("");
    signal.quality = vep_VSS_QUALITY_VALID;
    signal.value.type = vep_VSS_VALUE_TYPE_INT32;
    signal.value.int32_value = value;
    return signal;
}

vep_VssSignal create_string_signal(const char* path, const char* value, int64_t timestamp_ns) {
    vep_VssSignal signal = {};
    signal.path = const_cast<char*>(path);
    signal.header.source_id = const_cast<char*>("test");
    signal.header.timestamp_ns = timestamp_ns;
    signal.header.seq_num = 0;
    signal.header.correlation_id = const_cast<char*>("");
    signal.quality = vep_VSS_QUALITY_VALID;
    signal.value.type = vep_VSS_VALUE_TYPE_STRING;
    signal.value.string_value = const_cast<char*>(value);
    return signal;
}

vep_VssSignal create_bool_signal(const char* path, bool value, int64_t timestamp_ns) {
    vep_VssSignal signal = {};
    signal.path = const_cast<char*>(path);
    signal.header.source_id = const_cast<char*>("test");
    signal.header.timestamp_ns = timestamp_ns;
    signal.header.seq_num = 0;
    signal.header.correlation_id = const_cast<char*>("");
    signal.quality = vep_VSS_QUALITY_VALID;
    signal.value.type = vep_VSS_VALUE_TYPE_BOOL;
    signal.value.bool_value = value;
    return signal;
}

// =============================================================================
// Signal Round-Trip Tests
// =============================================================================

class SignalRoundTripTest : public ::testing::Test {
protected:
    SignalBatchBuilder builder_{"test_source"};
};

TEST_F(SignalRoundTripTest, DoubleValue) {
    auto signal = create_double_signal("Vehicle.Speed", 123.456, 1000000000);
    builder_.add(signal);

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 1);

    const auto& s = decoded->signals[0];
    EXPECT_EQ(s.path, "Vehicle.Speed");
    EXPECT_EQ(s.quality, DecodedQuality::VALID);
    ASSERT_TRUE(std::holds_alternative<double>(s.value));
    EXPECT_DOUBLE_EQ(std::get<double>(s.value), 123.456);
}

TEST_F(SignalRoundTripTest, Int32Value) {
    auto signal = create_int32_signal("Vehicle.Powertrain.ElectricMotor.Temperature", 85, 1000000000);
    builder_.add(signal);

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 1);

    const auto& s = decoded->signals[0];
    EXPECT_EQ(s.path, "Vehicle.Powertrain.ElectricMotor.Temperature");
    ASSERT_TRUE(std::holds_alternative<int32_t>(s.value));
    EXPECT_EQ(std::get<int32_t>(s.value), 85);
}

TEST_F(SignalRoundTripTest, StringValue) {
    auto signal = create_string_signal("Vehicle.VehicleIdentification.VIN", "5YJSA1E26MF123456", 1000000000);
    builder_.add(signal);

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 1);

    const auto& s = decoded->signals[0];
    EXPECT_EQ(s.path, "Vehicle.VehicleIdentification.VIN");
    ASSERT_TRUE(std::holds_alternative<std::string>(s.value));
    EXPECT_EQ(std::get<std::string>(s.value), "5YJSA1E26MF123456");
}

TEST_F(SignalRoundTripTest, BoolValue) {
    auto signal = create_bool_signal("Vehicle.Chassis.ParkingBrake.IsEngaged", true, 1000000000);
    builder_.add(signal);

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 1);

    const auto& s = decoded->signals[0];
    EXPECT_EQ(s.path, "Vehicle.Chassis.ParkingBrake.IsEngaged");
    ASSERT_TRUE(std::holds_alternative<bool>(s.value));
    EXPECT_TRUE(std::get<bool>(s.value));
}

TEST_F(SignalRoundTripTest, MultipleSignals) {
    builder_.add(create_double_signal("Vehicle.Speed", 100.0, 1000000000));
    builder_.add(create_int32_signal("Vehicle.Powertrain.TractionBattery.StateOfCharge.Current", 85, 1001000000));
    builder_.add(create_string_signal("Vehicle.VehicleIdentification.VIN", "ABC123", 1002000000));

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->signals.size(), 3);
    EXPECT_EQ(decoded->source_id, "test_source");

    EXPECT_EQ(decoded->signals[0].path, "Vehicle.Speed");
    EXPECT_EQ(decoded->signals[1].path, "Vehicle.Powertrain.TractionBattery.StateOfCharge.Current");
    EXPECT_EQ(decoded->signals[2].path, "Vehicle.VehicleIdentification.VIN");
}

TEST_F(SignalRoundTripTest, DeltaTimestamps) {
    int64_t base_ns = 1000000000;  // 1 second
    builder_.add(create_double_signal("S1", 1.0, base_ns));
    builder_.add(create_double_signal("S2", 2.0, base_ns + 10000000));   // +10ms
    builder_.add(create_double_signal("S3", 3.0, base_ns + 100000000));  // +100ms

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 3);

    // Verify absolute timestamps are reconstructed correctly
    EXPECT_EQ(decoded->signals[0].timestamp_ms, 1000);    // 1000ms
    EXPECT_EQ(decoded->signals[1].timestamp_ms, 1010);    // 1010ms
    EXPECT_EQ(decoded->signals[2].timestamp_ms, 1100);    // 1100ms
}

TEST_F(SignalRoundTripTest, QualityStates) {
    // Test all quality states
    vep_VssSignal signal = create_double_signal("Signal", 0.0, 1000000000);

    signal.quality = vep_VSS_QUALITY_VALID;
    builder_.add(signal);

    signal.quality = vep_VSS_QUALITY_INVALID;
    signal.path = const_cast<char*>("Signal2");
    builder_.add(signal);

    signal.quality = vep_VSS_QUALITY_NOT_AVAILABLE;
    signal.path = const_cast<char*>("Signal3");
    builder_.add(signal);

    auto data = builder_.build();
    auto decoded = decode_signal_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->signals.size(), 3);

    EXPECT_EQ(decoded->signals[0].quality, DecodedQuality::VALID);
    EXPECT_EQ(decoded->signals[1].quality, DecodedQuality::INVALID);
    EXPECT_EQ(decoded->signals[2].quality, DecodedQuality::NOT_AVAILABLE);
}

// =============================================================================
// Event Round-Trip Tests
// =============================================================================

class EventRoundTripTest : public ::testing::Test {
protected:
    EventBatchBuilder builder_{"test_source"};

    vep_Event create_event(const char* id, const char* category, vep_Severity severity) {
        vep_Event event = {};
        event.event_id = const_cast<char*>(id);
        event.header.source_id = const_cast<char*>("test");
        event.header.timestamp_ns = 1000000000;
        event.header.seq_num = 0;
        event.header.correlation_id = const_cast<char*>("");
        event.category = const_cast<char*>(category);
        event.event_type = const_cast<char*>("test_type");
        event.severity = severity;
        event.attributes._length = 0;
        event.attributes._maximum = 0;
        event.attributes._buffer = nullptr;
        event.context._length = 0;
        event.context._maximum = 0;
        event.context._buffer = nullptr;
        return event;
    }
};

TEST_F(EventRoundTripTest, SingleEvent) {
    builder_.add(create_event("evt_001", "diagnostic", vep_SEVERITY_WARNING));

    auto data = builder_.build();
    auto decoded = decode_event_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->events.size(), 1);

    const auto& e = decoded->events[0];
    EXPECT_EQ(e.event_id, "evt_001");
    EXPECT_EQ(e.category, "diagnostic");
    EXPECT_EQ(e.event_type, "test_type");
}

TEST_F(EventRoundTripTest, MultipleEvents) {
    builder_.add(create_event("evt_1", "vehicle", vep_SEVERITY_INFO));
    builder_.add(create_event("evt_2", "diagnostic", vep_SEVERITY_WARNING));
    builder_.add(create_event("evt_3", "safety", vep_SEVERITY_CRITICAL));

    auto data = builder_.build();
    auto decoded = decode_event_batch(data);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->events.size(), 3);
    EXPECT_EQ(decoded->events[0].category, "vehicle");
    EXPECT_EQ(decoded->events[1].category, "diagnostic");
    EXPECT_EQ(decoded->events[2].category, "safety");
}

// =============================================================================
// Metrics Round-Trip Tests
// =============================================================================

class MetricsRoundTripTest : public ::testing::Test {
protected:
    MetricsBatchBuilder builder_{"test_source"};

    vep_OtelGauge create_gauge(const char* name, double value) {
        vep_OtelGauge gauge = {};
        gauge.name = const_cast<char*>(name);
        gauge.header.source_id = const_cast<char*>("test");
        gauge.header.timestamp_ns = 1000000000;
        gauge.header.seq_num = 0;
        gauge.header.correlation_id = const_cast<char*>("");
        gauge.value = value;
        gauge.labels._length = 0;
        gauge.labels._maximum = 0;
        gauge.labels._buffer = nullptr;
        return gauge;
    }

    vep_OtelCounter create_counter(const char* name, double value) {
        vep_OtelCounter counter = {};
        counter.name = const_cast<char*>(name);
        counter.header.source_id = const_cast<char*>("test");
        counter.header.timestamp_ns = 1000000000;
        counter.header.seq_num = 0;
        counter.header.correlation_id = const_cast<char*>("");
        counter.value = value;
        counter.labels._length = 0;
        counter.labels._maximum = 0;
        counter.labels._buffer = nullptr;
        return counter;
    }
};

TEST_F(MetricsRoundTripTest, GaugeMetric) {
    builder_.add(create_gauge("cpu_usage_percent", 45.5));

    auto data = builder_.build();
    auto decoded = decode_metrics_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->metrics.size(), 1);

    const auto& m = decoded->metrics[0];
    EXPECT_EQ(m.name, "cpu_usage_percent");
    EXPECT_EQ(m.type, MetricType::GAUGE);
    EXPECT_DOUBLE_EQ(m.value, 45.5);
}

TEST_F(MetricsRoundTripTest, CounterMetric) {
    builder_.add(create_counter("http_requests_total", 12345.0));

    auto data = builder_.build();
    auto decoded = decode_metrics_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->metrics.size(), 1);

    const auto& m = decoded->metrics[0];
    EXPECT_EQ(m.name, "http_requests_total");
    EXPECT_EQ(m.type, MetricType::COUNTER);
    EXPECT_DOUBLE_EQ(m.value, 12345.0);
}

TEST_F(MetricsRoundTripTest, MixedMetrics) {
    builder_.add(create_gauge("memory_usage_mb", 2048.0));
    builder_.add(create_counter("errors_total", 42.0));
    builder_.add(create_gauge("temperature_celsius", 65.3));

    auto data = builder_.build();
    auto decoded = decode_metrics_batch(data);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->metrics.size(), 3);
}

// =============================================================================
// Log Round-Trip Tests
// =============================================================================

class LogRoundTripTest : public ::testing::Test {
protected:
    LogBatchBuilder builder_{"test_source"};

    vep_OtelLogEntry create_log(const char* component, const char* message, vep_OtelLogLevel level) {
        vep_OtelLogEntry log = {};
        log.header.source_id = const_cast<char*>("test");
        log.header.timestamp_ns = 1000000000;
        log.header.seq_num = 0;
        log.header.correlation_id = const_cast<char*>("");
        log.level = level;
        log.component = const_cast<char*>(component);
        log.message = const_cast<char*>(message);
        log.attributes._length = 0;
        log.attributes._maximum = 0;
        log.attributes._buffer = nullptr;
        log.trace_id = const_cast<char*>("");
        log.span_id = const_cast<char*>("");
        return log;
    }
};

TEST_F(LogRoundTripTest, SingleLog) {
    builder_.add(create_log("can_probe", "Frame received on vcan0", vep_LOG_LEVEL_INFO));

    auto data = builder_.build();
    auto decoded = decode_log_batch(data);

    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->entries.size(), 1);

    const auto& l = decoded->entries[0];
    EXPECT_EQ(l.component, "can_probe");
    EXPECT_EQ(l.message, "Frame received on vcan0");
    EXPECT_EQ(l.level, LogLevel::INFO);
    // Note: trace_id/span_id not in wire protocol, so empty
    EXPECT_EQ(l.trace_id, "");
    EXPECT_EQ(l.span_id, "");
}

TEST_F(LogRoundTripTest, MultipleLogs) {
    builder_.add(create_log("probe", "Starting", vep_LOG_LEVEL_INFO));
    builder_.add(create_log("probe", "Processing", vep_LOG_LEVEL_DEBUG));
    builder_.add(create_log("probe", "Warning occurred", vep_LOG_LEVEL_WARN));
    builder_.add(create_log("probe", "Error occurred", vep_LOG_LEVEL_ERROR));

    auto data = builder_.build();
    auto decoded = decode_log_batch(data);

    ASSERT_TRUE(decoded.has_value());
    EXPECT_EQ(decoded->entries.size(), 4);
    EXPECT_EQ(decoded->entries[0].level, LogLevel::INFO);
    EXPECT_EQ(decoded->entries[1].level, LogLevel::DEBUG);
    EXPECT_EQ(decoded->entries[2].level, LogLevel::WARN);
    EXPECT_EQ(decoded->entries[3].level, LogLevel::ERROR);
}

// =============================================================================
// Utility Function Tests
// =============================================================================

TEST(DecoderUtilityTest, QualityToString) {
    EXPECT_STREQ(quality_to_string(DecodedQuality::VALID), "VALID");
    EXPECT_STREQ(quality_to_string(DecodedQuality::INVALID), "INVALID");
    EXPECT_STREQ(quality_to_string(DecodedQuality::NOT_AVAILABLE), "NOT_AVAILABLE");
    EXPECT_STREQ(quality_to_string(DecodedQuality::UNKNOWN), "UNKNOWN");
}

TEST(DecoderUtilityTest, MetricTypeToString) {
    EXPECT_STREQ(metric_type_to_string(MetricType::GAUGE), "gauge");
    EXPECT_STREQ(metric_type_to_string(MetricType::COUNTER), "counter");
    EXPECT_STREQ(metric_type_to_string(MetricType::HISTOGRAM), "histogram");
}

TEST(DecoderUtilityTest, LogLevelToString) {
    EXPECT_STREQ(log_level_to_string(LogLevel::DEBUG), "DEBUG");
    EXPECT_STREQ(log_level_to_string(LogLevel::INFO), "INFO");
    EXPECT_STREQ(log_level_to_string(LogLevel::WARN), "WARN");
    EXPECT_STREQ(log_level_to_string(LogLevel::ERROR), "ERROR");
}

TEST(DecoderUtilityTest, ValueTypeName) {
    EXPECT_EQ(value_type_name(std::monostate{}), "empty");
    EXPECT_EQ(value_type_name(true), "bool");
    EXPECT_EQ(value_type_name(int32_t(42)), "int32");
    EXPECT_EQ(value_type_name(int64_t(42)), "int64");
    EXPECT_EQ(value_type_name(double(3.14)), "double");
    EXPECT_EQ(value_type_name(std::string("hello")), "string");
    EXPECT_EQ(value_type_name(std::vector<double>{1.0, 2.0}), "double[]");
}

TEST(DecoderUtilityTest, ValueToString) {
    EXPECT_EQ(value_to_string(std::monostate{}), "<empty>");
    EXPECT_EQ(value_to_string(true), "true");
    EXPECT_EQ(value_to_string(false), "false");
    EXPECT_EQ(value_to_string(int32_t(42)), "42");
    EXPECT_EQ(value_to_string(std::string("hello")), "\"hello\"");
    EXPECT_EQ(value_to_string(std::vector<int32_t>{1, 2, 3}), "[1, 2, 3]");
}

// =============================================================================
// Error Handling Tests
// =============================================================================

TEST(DecoderErrorTest, InvalidData) {
    std::vector<uint8_t> garbage = {0x00, 0x01, 0x02, 0x03, 0xFF};

    auto signals = decode_signal_batch(garbage);
    auto events = decode_event_batch(garbage);
    auto metrics = decode_metrics_batch(garbage);
    auto logs = decode_log_batch(garbage);

    // Should return nullopt for invalid data
    EXPECT_FALSE(signals.has_value());
    EXPECT_FALSE(events.has_value());
    EXPECT_FALSE(metrics.has_value());
    EXPECT_FALSE(logs.has_value());
}

TEST(DecoderErrorTest, EmptyData) {
    std::vector<uint8_t> empty;

    auto signals = decode_signal_batch(empty);
    auto events = decode_event_batch(empty);
    auto metrics = decode_metrics_batch(empty);
    auto logs = decode_log_batch(empty);

    // Empty protobuf is technically valid, should decode to empty batch
    // (protobuf allows empty messages)
    EXPECT_TRUE(signals.has_value());
    EXPECT_TRUE(events.has_value());
    EXPECT_TRUE(metrics.has_value());
    EXPECT_TRUE(logs.has_value());

    EXPECT_EQ(signals->signals.size(), 0);
    EXPECT_EQ(events->events.size(), 0);
    EXPECT_EQ(metrics->metrics.size(), 0);
    EXPECT_EQ(logs->entries.size(), 0);
}

}  // namespace vep::exporter::test
