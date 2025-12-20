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
    // Vehicle transport - subscribes to c2v
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_ids = {42};  // Subscribe to content_id 42

    MqttBackendTransport vehicle(vehicle_config);

    // Cloud transport - sends to c2v (uses swapped prefixes)
    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";  // Cloud publishes to c2v
    cloud_config.c2v_prefix = "v2c";  // Cloud subscribes to v2c

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
    uint32_t sent_content_id = 42;

    // Cloud sends to vehicle
    ASSERT_TRUE(cloud.publish(sent_content_id, sent_data));

    // Wait for vehicle to receive
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool received = cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            return !received_data.empty();
        });
        ASSERT_TRUE(received) << "Vehicle did not receive data within timeout";
    }

    // Verify data integrity
    EXPECT_EQ(received_content_id, sent_content_id);
    EXPECT_EQ(received_data.size(), sent_data.size());
    EXPECT_EQ(received_data, sent_data) << "Received data does not match sent data";

    vehicle.stop();
    cloud.stop();
}

// Core test: Vehicle sends opaque data, cloud receives it via callback
TEST_F(TransportTest, OpaqueDataFlowsVehicleToCloud) {
    // Vehicle transport
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;

    MqttBackendTransport vehicle(vehicle_config);

    // Cloud transport - subscribes to v2c (uses swapped prefixes)
    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";
    cloud_config.c2v_prefix = "v2c";
    cloud_config.content_ids = {99};  // Subscribe to content_id 99

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
    uint32_t sent_content_id = 99;

    // Vehicle sends to cloud
    ASSERT_TRUE(vehicle.publish(sent_content_id, sent_data));

    // Wait for cloud to receive
    {
        std::unique_lock<std::mutex> lock(mtx);
        bool received = cv.wait_for(lock, std::chrono::seconds(5), [&]() {
            return !received_data.empty();
        });
        ASSERT_TRUE(received) << "Cloud did not receive data within timeout";
    }

    // Verify data integrity
    EXPECT_EQ(received_content_id, sent_content_id);
    EXPECT_EQ(received_data.size(), sent_data.size());
    EXPECT_EQ(received_data, sent_data) << "Received data does not match sent data";

    vehicle.stop();
    cloud.stop();
}

// Test: Large opaque payload
TEST_F(TransportTest, LargeOpaquePayload) {
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_ids = {1};

    MqttBackendTransport vehicle(vehicle_config);

    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";
    cloud_config.c2v_prefix = "v2c";

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

    ASSERT_TRUE(cloud.publish(1, sent_data));

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

// Test: Multiple content IDs
TEST_F(TransportTest, MultipleContentIds) {
    MqttBackendTransportConfig vehicle_config;
    vehicle_config.broker_host = getMqttHost();
    vehicle_config.broker_port = getMqttPort();
    vehicle_config.client_id = "vehicle_" + vehicle_id_;
    vehicle_config.vehicle_id = vehicle_id_;
    vehicle_config.content_ids = {1, 2, 3};  // Subscribe to multiple

    MqttBackendTransport vehicle(vehicle_config);

    MqttBackendTransportConfig cloud_config;
    cloud_config.broker_host = getMqttHost();
    cloud_config.broker_port = getMqttPort();
    cloud_config.client_id = "cloud_" + vehicle_id_;
    cloud_config.vehicle_id = vehicle_id_;
    cloud_config.v2c_prefix = "c2v";
    cloud_config.c2v_prefix = "v2c";

    MqttBackendTransport cloud(cloud_config);

    std::mutex mtx;
    std::condition_variable cv;
    std::map<uint32_t, std::vector<uint8_t>> received;

    vehicle.on_content([&](uint32_t content_id, const std::vector<uint8_t>& payload) {
        std::lock_guard<std::mutex> lock(mtx);
        received[content_id] = payload;
        cv.notify_all();
    });

    ASSERT_TRUE(vehicle.start());
    ASSERT_TRUE(cloud.start());
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // Send to different content IDs
    std::vector<uint8_t> data1 = {0x01};
    std::vector<uint8_t> data2 = {0x02, 0x02};
    std::vector<uint8_t> data3 = {0x03, 0x03, 0x03};

    ASSERT_TRUE(cloud.publish(1, data1));
    ASSERT_TRUE(cloud.publish(2, data2));
    ASSERT_TRUE(cloud.publish(3, data3));

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

    vehicle.stop();
    cloud.stop();
}

}  // namespace
}  // namespace vep
