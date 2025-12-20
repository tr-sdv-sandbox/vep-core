// Copyright 2025 Vehicle Edge Platform Contributors
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

// Helper to create an OtelCounter
vep_OtelCounter create_test_counter(const char* name, double value) {
    vep_OtelCounter counter = {};
    counter.header.source_id = const_cast<char*>("test");
    counter.header.timestamp_ns = 1000000000;
    counter.header.seq_num = 0;
    counter.header.correlation_id = const_cast<char*>("");
    counter.name = const_cast<char*>(name);
    counter.value = value;
    counter.labels._length = 0;
    counter.labels._maximum = 0;
    counter.labels._buffer = nullptr;
    return counter;
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
// UnifiedBatchBuilder Tests
// =============================================================================

class UnifiedBatchBuilderTest : public ::testing::Test {
protected:
    UnifiedBatchBuilder builder_{"test_source", 100};
};

TEST_F(UnifiedBatchBuilderTest, InitiallyEmpty) {
    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
    EXPECT_FALSE(builder_.full());
}

TEST_F(UnifiedBatchBuilderTest, AddSingleSignal) {
    auto signal = create_test_signal("Vehicle.Speed", 100.5, 1000000000);
    builder_.add(signal);

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 1);
}

TEST_F(UnifiedBatchBuilderTest, AddMultipleSignals) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.add(create_test_signal("Vehicle.Powertrain.TractionBattery.StateOfCharge.Current", 85.0, 1001000000));
    builder_.add(create_test_signal("Vehicle.CurrentLocation.Latitude", 37.7749, 1002000000));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 3);
}

TEST_F(UnifiedBatchBuilderTest, AddMixedTypes) {
    // Add one of each type
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.add(create_test_event("evt_1", "vehicle", vep_SEVERITY_WARNING));
    builder_.add(create_test_gauge("cpu_usage", 45.5));
    builder_.add(create_test_log("can_probe", "Received CAN frame", vep_LOG_LEVEL_INFO));

    EXPECT_TRUE(builder_.ready());
    EXPECT_EQ(builder_.size(), 4);
}

TEST_F(UnifiedBatchBuilderTest, BuildProducesValidProtobuf) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));

    auto data = builder_.build();
    EXPECT_FALSE(data.empty());

    // Verify protobuf can be parsed
    vep::transfer::TransferBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));
    EXPECT_EQ(batch.items_size(), 1);
    EXPECT_TRUE(batch.items(0).has_signal());
    EXPECT_EQ(batch.items(0).signal().path(), "Vehicle.Speed");
}

TEST_F(UnifiedBatchBuilderTest, BuildClearsBatch) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    auto data = builder_.build();

    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(UnifiedBatchBuilderTest, ResetClearsBatch) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.reset();

    EXPECT_FALSE(builder_.ready());
    EXPECT_EQ(builder_.size(), 0);
}

TEST_F(UnifiedBatchBuilderTest, DeltaTimestampsAreCalculated) {
    // All signals with timestamps relative to a base
    int64_t base_ns = 1000000000;
    builder_.add(create_test_signal("Signal1", 1.0, base_ns));
    builder_.add(create_test_signal("Signal2", 2.0, base_ns + 10000000));   // +10ms
    builder_.add(create_test_signal("Signal3", 3.0, base_ns + 20000000));   // +20ms

    auto data = builder_.build();
    vep::transfer::TransferBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));

    EXPECT_EQ(batch.items_size(), 3);
    // Base timestamp should be set
    EXPECT_GT(batch.base_timestamp_ms(), 0);

    // Delta timestamps should be relative to base
    int64_t base_ms = batch.base_timestamp_ms();
    int64_t expected_base_ms = base_ns / 1000000;  // Convert ns to ms
    EXPECT_EQ(base_ms, expected_base_ms);

    // First item should have delta 0
    EXPECT_EQ(batch.items(0).timestamp_delta_ms(), 0);
    // Second item should have delta 10
    EXPECT_EQ(batch.items(1).timestamp_delta_ms(), 10);
    // Third item should have delta 20
    EXPECT_EQ(batch.items(2).timestamp_delta_ms(), 20);
}

TEST_F(UnifiedBatchBuilderTest, SequenceNumberIncrementsAcrossBatches) {
    // First batch
    builder_.add(create_test_signal("Signal1", 1.0, 1000000000));
    auto data1 = builder_.build();
    vep::transfer::TransferBatch batch1;
    EXPECT_TRUE(batch1.ParseFromArray(data1.data(), static_cast<int>(data1.size())));

    // Second batch
    builder_.add(create_test_signal("Signal2", 2.0, 1000000000));
    auto data2 = builder_.build();
    vep::transfer::TransferBatch batch2;
    EXPECT_TRUE(batch2.ParseFromArray(data2.data(), static_cast<int>(data2.size())));

    EXPECT_NE(batch1.sequence(), batch2.sequence());
    EXPECT_EQ(batch2.sequence(), batch1.sequence() + 1);
}

TEST_F(UnifiedBatchBuilderTest, FullReturnsTrueAtCapacity) {
    UnifiedBatchBuilder small_builder("test", 3);

    EXPECT_FALSE(small_builder.full());

    small_builder.add(create_test_signal("Signal1", 1.0, 1000000000));
    EXPECT_FALSE(small_builder.full());

    small_builder.add(create_test_signal("Signal2", 2.0, 1000000000));
    EXPECT_FALSE(small_builder.full());

    small_builder.add(create_test_signal("Signal3", 3.0, 1000000000));
    EXPECT_TRUE(small_builder.full());
}

TEST_F(UnifiedBatchBuilderTest, EstimatedSizeIncreases) {
    size_t initial_size = builder_.estimated_size();
    EXPECT_EQ(initial_size, 0);

    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    size_t size_after_one = builder_.estimated_size();
    EXPECT_GT(size_after_one, initial_size);

    builder_.add(create_test_signal("Vehicle.RPM", 3000.0, 1000000000));
    size_t size_after_two = builder_.estimated_size();
    EXPECT_GT(size_after_two, size_after_one);
}

TEST_F(UnifiedBatchBuilderTest, ThreadSafety) {
    const int NUM_THREADS = 4;
    const int ITEMS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    for (int t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([this, t]() {
            for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
                std::string path = "Signal_" + std::to_string(t) + "_" + std::to_string(i);
                // Use static buffer for test
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

    // All items should be added
    EXPECT_EQ(builder_.size(), NUM_THREADS * ITEMS_PER_THREAD);
}

TEST_F(UnifiedBatchBuilderTest, MixedTypesProduceCorrectItemTypes) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));
    builder_.add(create_test_event("evt_1", "diagnostic", vep_SEVERITY_ERROR));
    builder_.add(create_test_gauge("memory_usage", 8192.0));
    builder_.add(create_test_log("exporter", "Connection established", vep_LOG_LEVEL_INFO));

    auto data = builder_.build();
    vep::transfer::TransferBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));

    EXPECT_EQ(batch.items_size(), 4);

    // Items should be in order of addition
    EXPECT_TRUE(batch.items(0).has_signal());
    EXPECT_EQ(batch.items(0).signal().path(), "Vehicle.Speed");

    EXPECT_TRUE(batch.items(1).has_event());
    EXPECT_EQ(batch.items(1).event().event_id(), "evt_1");

    EXPECT_TRUE(batch.items(2).has_metric());
    EXPECT_EQ(batch.items(2).metric().name(), "memory_usage");
    EXPECT_TRUE(batch.items(2).metric().has_gauge());

    EXPECT_TRUE(batch.items(3).has_log());
    EXPECT_EQ(batch.items(3).log().component(), "exporter");
}

TEST_F(UnifiedBatchBuilderTest, CounterMetricType) {
    builder_.add(create_test_counter("request_count", 1000.0));

    auto data = builder_.build();
    vep::transfer::TransferBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));

    EXPECT_EQ(batch.items_size(), 1);
    EXPECT_TRUE(batch.items(0).has_metric());
    EXPECT_TRUE(batch.items(0).metric().has_counter());
    EXPECT_DOUBLE_EQ(batch.items(0).metric().counter(), 1000.0);
}

TEST_F(UnifiedBatchBuilderTest, SourceIdIsSet) {
    builder_.add(create_test_signal("Vehicle.Speed", 100.5, 1000000000));

    auto data = builder_.build();
    vep::transfer::TransferBatch batch;
    EXPECT_TRUE(batch.ParseFromArray(data.data(), static_cast<int>(data.size())));

    EXPECT_EQ(batch.source_id(), "test_source");
}

}  // namespace vep::exporter::test
