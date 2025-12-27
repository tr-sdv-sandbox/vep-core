// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file transport_test.cpp
/// @brief Bidirectional transport tests - proves opaque data flows correctly

#include "vep/backend_transport.hpp"
#include "vep/mqtt_backend_transport.hpp"
#include "mqtt_test_fixture.hpp"

#include <gtest/gtest.h>
#include <glog/logging.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <thread>

namespace vep {
namespace {

class TransportTest : public test::MqttTestFixture {
protected:
    void SetUp() override {
        test::MqttTestFixture::SetUp();
        // Unique vehicle ID to avoid test interference
        std::random_device rd;
        vehicle_id_ = "test_" + std::to_string(rd() % 100000);
    }

    std::string vehicle_id_;
};

// Generate random opaque data
std::vector<uint8_t> generate_opaque_data(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);
    for (auto& byte : data) {
        byte = static_cast<uint8_t>(dis(gen));
    }
    return data;
}

// Core test: Cloud sends opaque data, vehicle receives it via callback
TEST_F(TransportTest, OpaqueDataFlowsCloudToVehicle) {
    const uint32_t test_content_id = 42;

    // Vehicle transport - bound to content_id 42
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_id = test_content_id;

    MqttBackendTransport vehicle(vehicle_config);

    // Cloud transport - sends to c2v (uses swapped prefixes)
    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";  // Cloud publishes to c2v
    cloud_config.c2v_prefix = "v2c";  // Cloud subscribes to v2c
    cloud_config.content_id = test_content_id;

    MqttBackendTransport cloud(cloud_config);

    // Capture received data
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t received_content_id = 0;
    std::vector<uint8_t> received_data;

    vehicle.on_content([&](uint32_t content_id, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        received_content_id = content_id;
        received_data = payload;
        cv.notify_all();
    });

    // Start transports
    ASSERT_TRUE(vehicle.start());
    ASSERT_TRUE(cloud.start());

    // Wait for connections
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Generate opaque test data
    auto sent_data = generate_opaque_data(256);

    // Cloud sends to vehicle (uses bound content_id)
    ASSERT_TRUE(cloud.publish(sent_data));

    // Wait for vehicle to receive
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool received = cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            return !received_data.empty();
        });
        ASSERT_TRUE(received) << "Vehicle did not receive data within timeout";
    }

    // Verify data integrity
    EXPECT_EQ(received_content_id, test_content_id);
    EXPECT_EQ(received_data.size(), sent_data.size());
    EXPECT_EQ(received_data, sent_data) << "Received data does not match sent data";

    vehicle.stop();
    cloud.stop();
}

// Core test: Vehicle sends opaque data, cloud receives it via callback
TEST_F(TransportTest, OpaqueDataFlowsVehicleToCloud) {
    const uint32_t test_content_id = 99;

    // Vehicle transport - bound to content_id 99
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_id = test_content_id;

    MqttBackendTransport vehicle(vehicle_config);

    // Cloud transport - subscribes to v2c (uses swapped prefixes)
    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";
    cloud_config.c2v_prefix = "v2c";
    cloud_config.content_id = test_content_id;

    MqttBackendTransport cloud(cloud_config);

    // Capture received data
    std::mutex mtx;
    std::condition_variable cv;
    uint32_t received_content_id = 0;
    std::vector<uint8_t> received_data;

    cloud.on_content([&](uint32_t content_id, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        received_content_id = content_id;
        received_data = payload;
        cv.notify_all();
    });

    // Start transports
    ASSERT_TRUE(vehicle.start());
    ASSERT_TRUE(cloud.start());

    // Wait for connections
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Generate opaque test data
    auto sent_data = generate_opaque_data(1024);

    // Vehicle sends to cloud (uses bound content_id)
    ASSERT_TRUE(vehicle.publish(sent_data));

    // Wait for cloud to receive
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool received = cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            return !received_data.empty();
        });
        ASSERT_TRUE(received) << "Cloud did not receive data within timeout";
    }

    // Verify data integrity
    EXPECT_EQ(received_content_id, test_content_id);
    EXPECT_EQ(received_data.size(), sent_data.size());
    EXPECT_EQ(received_data, sent_data) << "Received data does not match sent data";

    vehicle.stop();
    cloud.stop();
}

// Test: Large opaque payload
TEST_F(TransportTest, LargeOpaquePayload) {
    const uint32_t test_content_id = 1;

    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_id = test_content_id;

    MqttBackendTransport vehicle(vehicle_config);

    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";
    cloud_config.c2v_prefix = "v2c";
    cloud_config.content_id = test_content_id;

    MqttBackendTransport cloud(cloud_config);

    std::mutex mtx;
    std::condition_variable cv;
    std::vector<uint8_t> received_data;

    vehicle.on_content([&](uint32_t, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        received_data = payload;
        cv.notify_all();
    });

    ASSERT_TRUE(vehicle.start());
    ASSERT_TRUE(cloud.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 64KB opaque data
    auto sent_data = generate_opaque_data(64 * 1024);

    ASSERT_TRUE(cloud.publish(sent_data));

    {
        std::unique_lock<std::mutex> lock(mtx);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(10), [&]() {
            return !received_data.empty();
        }));
    }

    EXPECT_EQ(received_data, sent_data);

    vehicle.stop();
    cloud.stop();
}

// Test: Multiple content IDs require separate transport instances
TEST_F(TransportTest, MultipleContentIds) {
    // Create separate transports for each content_id
    // Vehicle side - 3 transports for 3 content_ids
    MqttBackendTransportConfig vehicle_config1;
    vehicle_config1.broker_host = getMqttHost();
    vehicle_config1.broker_port = getMqttPort();
    vehicle_config1.client_id = "vehicle1_" + vehicle_id_;
    vehicle_config1.vehicle_id = vehicle_id_;
    vehicle_config1.content_id = 1;

    MqttBackendTransportConfig vehicle_config2;
    vehicle_config2.broker_host = getMqttHost();
    vehicle_config2.broker_port = getMqttPort();
    vehicle_config2.client_id = "vehicle2_" + vehicle_id_;
    vehicle_config2.vehicle_id = vehicle_id_;
    vehicle_config2.content_id = 2;

    MqttBackendTransportConfig vehicle_config3;
    vehicle_config3.broker_host = getMqttHost();
    vehicle_config3.broker_port = getMqttPort();
    vehicle_config3.client_id = "vehicle3_" + vehicle_id_;
    vehicle_config3.vehicle_id = vehicle_id_;
    vehicle_config3.content_id = 3;

    MqttBackendTransport vehicle1(vehicle_config1);
    MqttBackendTransport vehicle2(vehicle_config2);
    MqttBackendTransport vehicle3(vehicle_config3);

    // Cloud side - 3 transports (swapped prefixes)
    MqttBackendTransportConfig cloud_config1;
    cloud_config1.broker_host = getMqttHost();
    cloud_config1.broker_port = getMqttPort();
    cloud_config1.client_id = "cloud1_" + vehicle_id_;
    cloud_config1.vehicle_id = vehicle_id_;
    cloud_config1.v2c_prefix = "c2v";
    cloud_config1.c2v_prefix = "v2c";
    cloud_config1.content_id = 1;

    MqttBackendTransportConfig cloud_config2;
    cloud_config2.broker_host = getMqttHost();
    cloud_config2.broker_port = getMqttPort();
    cloud_config2.client_id = "cloud2_" + vehicle_id_;
    cloud_config2.vehicle_id = vehicle_id_;
    cloud_config2.v2c_prefix = "c2v";
    cloud_config2.c2v_prefix = "v2c";
    cloud_config2.content_id = 2;

    MqttBackendTransportConfig cloud_config3;
    cloud_config3.broker_host = getMqttHost();
    cloud_config3.broker_port = getMqttPort();
    cloud_config3.client_id = "cloud3_" + vehicle_id_;
    cloud_config3.vehicle_id = vehicle_id_;
    cloud_config3.v2c_prefix = "c2v";
    cloud_config3.c2v_prefix = "v2c";
    cloud_config3.content_id = 3;

    MqttBackendTransport cloud1(cloud_config1);
    MqttBackendTransport cloud2(cloud_config2);
    MqttBackendTransport cloud3(cloud_config3);

    std::mutex mtx;
    std::condition_variable cv;
    std::map<uint32_t, std::vector<uint8_t>> received;

    auto on_recv = [&](uint32_t content_id, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        received[content_id] = payload;
        cv.notify_all();
    };

    vehicle1.on_content(on_recv);
    vehicle2.on_content(on_recv);
    vehicle3.on_content(on_recv);

    ASSERT_TRUE(vehicle1.start());
    ASSERT_TRUE(vehicle2.start());
    ASSERT_TRUE(vehicle3.start());
    ASSERT_TRUE(cloud1.start());
    ASSERT_TRUE(cloud2.start());
    ASSERT_TRUE(cloud3.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send to different content IDs via different cloud transports
    std::vector<uint8_t> data1 = {0x01};
    std::vector<uint8_t> data2 = {0x02, 0x02};
    std::vector<uint8_t> data3 = {0x03, 0x03, 0x03};

    ASSERT_TRUE(cloud1.publish(data1));
    ASSERT_TRUE(cloud2.publish(data2));
    ASSERT_TRUE(cloud3.publish(data3));

    // Wait for all to arrive
    {
        std::unique_lock<std::mutex> lock(mtx);
        ASSERT_TRUE(cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            return received.size() == 3;
        }));
    }

    EXPECT_EQ(received[1], data1);
    EXPECT_EQ(received[2], data2);
    EXPECT_EQ(received[3], data3);

    vehicle1.stop();
    vehicle2.stop();
    vehicle3.stop();
    cloud1.stop();
    cloud2.stop();
    cloud3.stop();
}

// Test: content_id() accessor returns configured value
TEST_F(TransportTest, ContentIdAccessor) {
    MqttBackendTransportConfig config;
    config.broker_host = getMqttHost();
    config.broker_port = getMqttPort();
    config.client_id = "test_" + vehicle_id_;
    config.vehicle_id = vehicle_id_;
    config.content_id = 42;

    MqttBackendTransport transport(config);
    EXPECT_EQ(transport.content_id(), 42u);
}

}  // namespace
}  // namespace vep
