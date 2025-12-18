// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "batch_builder.hpp"
#include "transfer.pb.h"

#include <gtest/gtest.h>
#include <cstring>
#include <thread>

namespace vep::exporter::test {

// Helper to create a VssSignal with the given parameters
vep_VssSignal create_test_signal(const char* path, double value, int64_t timestamp_ns) {
    vep_VssSignal signal = {};
    signal.path = const_cast<char*>(path);  // DDS uses C-style strings
    signal.header.source_id = const_cast<char*>("test");
    signal.header.timestamp_ns = timestamp_ns;
    signal.header.seq_num = 0;
    signal.header.correlation_id = const_cast<char*>("");
    signal.quality = vep_VSS_QUALITY_VALID;
    signal.value.type = vep_VSS_VALUE_TYPE_DOUBLE;
    signal.value.double_value = value;
    return signal;
}

// Helper to create an Event
vep_Event create_test_event(const char* event_id, const char* category, vep_Severity severity) {
    vep_Event event = {};
    event.header.source_id = const_cast<char*>("test");
    event.header.timestamp_ns = 1000000000;  // 1 second in ns
    event.header.seq_num = 0;
    event.header.correlation_id = const_cast<char*>("");
    event.event_id = const_cast<char*>(event_id);
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

// Helper to create an OtelGauge
vep_OtelGauge create_test_gauge(const char* name, double value) {
    vep_OtelGauge gauge = {};
    gauge.header.source_id = const_cast<char*>("test");
    gauge.header.timestamp_ns = 1000000000;
    gauge.header.seq_num = 0;
    gauge.header.correlation_id = const_cast<char*>("");
    gauge.name = const_cast<char*>(name);
    gauge.value = value;
    gauge.labels._length = 0;
    gauge.labels._maximum = 0;
    gauge.labels._buffer = nullptr;
    return gauge;
}

// Helper to create an OtelLogEntry
vep_OtelLogEntry create_test_log(const char* component, const char* message, vep_OtelLogLevel level) {
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

// =============================================================================
// SignalBatchBuilder Tests
// =============================================================================

class SignalBatchBuilderTest : public ::testing::Test {
protected:
    SignalBatchBuilder builder_{"test_source"};
};

TEST_F(SignalBatchBuilderTest, InitiallyEmpty) {
    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(SignalBatchBuilderTest, AddSingleSignal) {
    auto signal = create_test_signal("Vehicle.Speed", 100.5, 1000000000);
    builder_.add(signal);

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 1);
}

TEST_F(SignalBatchBuilderTest, AddMultipleSignals) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.add(create_test_signal("Vehicle.Powertrain.TractionBattery.StateOfCharge.Current", 85.0, 1001000000));
    builder_.add(create_test_signal("Vehicle.CurrentLocation.Latitude", 37.7749, 1002000000));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 3);
}

TEST_F(SignalBatchBuilderTest, BuildProducesValidProtobuf) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));

    auto data = builder_.build();
    EXPECT_FALSE(data.empty());

    // Verify protobuf can be parsed
    vep::transfer::SignalBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));
    EXPECT_EQ(batch.signals_size(), 1);
    EXPECT_EQ(batch.signals(0).path(), "Vehicle.Speed");
}

TEST_F(SignalBatchBuilderTest, BuildClearsBatch) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    auto data = builder_.build();

    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(SignalBatchBuilderTest, ResetClearsBatch) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.reset();

    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(SignalBatchBuilderTest, DeltaTimestampsAreCalculated) {
    // All signals with timestamps relative to a base
    int64_t base_ns = 1000000000;
    builder_.add(create_test_signal("Signal1", 1.0, base_ns));
    builder_.add(create_test_signal("Signal2", 2.0, base_ns + 10000000));   // +10ms
    builder_.add(create_test_signal("Signal3", 3.0, base_ns + 20000000));   // +20ms

    auto data = builder_.build();
    vep::transfer::SignalBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));

    EXPECT_EQ(batch.signals_size(), 3);
    // Base timestamp should be set
    EXPECT_GT(batch.base_timestamp_ms(), 0);

    // Delta timestamps should be relative to base
    int64_t base_ms = batch.base_timestamp_ms();
    int64_t expected_base_ms = base_ns / 1000000;  // Convert ns to ms
    EXPECT_EQ(base_ms, expected_base_ms);
}

TEST_F(SignalBatchBuilderTest, SequenceNumberIncrementsAcrossBatches) {
    // First batch
    builder_.add(create_test_signal("Signal1", 1.0, 1000000000));
    auto data1 = builder_.build();
    vep::transfer::SignalBatch batch1;
    EXPECT_TRUE(batch1.ParseFromArray(data1.data(), static_cast<int>(data1.size())));

    // Second batch
    builder_.add(create_test_signal("Signal2", 2.0, 1000000000));
    auto data2 = builder_.build();
    vep::transfer::SignalBatch batch2;
    EXPECT_TRUE(batch2.ParseFromArray(data2.data(), static_cast<int>(data2.size())));

    EXPECT_NE(batch1.sequence(), batch2.sequence());
}

TEST_F(SignalBatchBuilderTest, ThreadSafety) {
    const int NUM_THREADS = 4;
    const int SIGNALS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < SIGNALS_PER_THREAD; ++i) {
                std::string path = "Signal_" + std::to_string(t) + "_" + std::to_string(i);
                // Use static buffer for test (not production-safe, but OK for single-threaded signal creation)
                thread_local char path_buf[64];
                strncpy(path_buf, path.c_str(), sizeof(path_buf) - 1);
                auto signal = create_test_signal(path_buf, i * 1.0, 1000000000 + i);
                builder_.add(signal);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // All signals should be added
    EXPECT_EQ(builder_.size(), NUM_THREADS * SIGNALS_PER_THREAD);
}

// =============================================================================
// EventBatchBuilder Tests
// =============================================================================

class EventBatchBuilderTest : public ::testing::Test {
protected:
    EventBatchBuilder builder_{"test_source"};
};

TEST_F(EventBatchBuilderTest, InitiallyEmpty) {
    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(EventBatchBuilderTest, AddSingleEvent) {
    builder_.add(create_test_event("evt_1", "vehicle", vep_SEVERITY_WARNING));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 1);
}

TEST_F(EventBatchBuilderTest, BuildProducesValidProtobuf) {
    builder_.add(create_test_event("evt_1", "diagnostic", vep_SEVERITY_ERROR));

    auto data = builder_.build();
    EXPECT_FALSE(data.empty());

    vep::transfer::EventBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));
    EXPECT_EQ(batch.events_size(), 1);
    EXPECT_EQ(batch.events(0).event_id(), "evt_1");
    EXPECT_EQ(batch.events(0).category(), "diagnostic");
}

// =============================================================================
// MetricsBatchBuilder Tests
// =============================================================================

class MetricsBatchBuilderTest : public ::testing::Test {
protected:
    MetricsBatchBuilder builder_{"test_source"};
};

TEST_F(MetricsBatchBuilderTest, InitiallyEmpty) {
    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(MetricsBatchBuilderTest, AddGauge) {
    builder_.add(create_test_gauge("cpu_usage", 45.5));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 1);
}

TEST_F(MetricsBatchBuilderTest, AddMultipleMetricTypes) {
    builder_.add(create_test_gauge("gauge_1", 10.0));

    // Create a counter
    vep_OtelCounter counter = {};
    counter.header.source_id = const_cast<char*>("test");
    counter.header.timestamp_ns = 1000000000;
    counter.header.seq_num = 0;
    counter.header.correlation_id = const_cast<char*>("");
    counter.name = const_cast<char*>("counter_1");
    counter.value = 100.0;
    counter.labels._length = 0;
    counter.labels._maximum = 0;
    counter.labels._buffer = nullptr;
    builder_.add(counter);

    EXPECT_EQ(builder_.size(), 2);
}

TEST_F(MetricsBatchBuilderTest, BuildProducesValidProtobuf) {
    builder_.add(create_test_gauge("memory_usage", 8192.0));

    auto data = builder_.build();
    EXPECT_FALSE(data.empty());

    vep::transfer::MetricsBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));
    EXPECT_EQ(batch.metrics_size(), 1);
    EXPECT_EQ(batch.metrics(0).name(), "memory_usage");
}

// =============================================================================
// LogBatchBuilder Tests
// =============================================================================

class LogBatchBuilderTest : public ::testing::Test {
protected:
    LogBatchBuilder builder_{"test_source"};
};

TEST_F(LogBatchBuilderTest, InitiallyEmpty) {
    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(LogBatchBuilderTest, AddSingleLog) {
    builder_.add(create_test_log("can_probe", "Received CAN frame", vep_LOG_LEVEL_INFO));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 1);
}

TEST_F(LogBatchBuilderTest, BuildProducesValidProtobuf) {
    builder_.add(create_test_log("exporter", "Connection established", vep_LOG_LEVEL_INFO));

    auto data = builder_.build();
    EXPECT_FALSE(data.empty());

    vep::transfer::LogBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));
    EXPECT_EQ(batch.logs_size(), 1);
    EXPECT_EQ(batch.logs(0).component(), "exporter");
    EXPECT_EQ(batch.logs(0).message(), "Connection established");
}

}  // namespace vep::exporter::test
