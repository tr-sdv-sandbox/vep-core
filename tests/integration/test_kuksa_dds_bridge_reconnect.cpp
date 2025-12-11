// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

/**
 * @file test_kuksa_dds_bridge_reconnect.cpp
 * @brief Integration tests for kuksa_dds_bridge reconnection logic
 *
 * Tests verify:
 * 1. Bridge fails to initialize when KUKSA is not available
 * 2. Bridge successfully initializes when KUKSA becomes available
 * 3. Bridge can be stopped and restarted cleanly
 */

#include <gtest/gtest.h>
#include <glog/logging.h>

#include "bridge/kuksa_dds_bridge.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

using namespace std::chrono_literals;

namespace {

constexpr const char* KUKSA_IMAGE = "ghcr.io/eclipse-kuksa/kuksa-databroker:0.6.0";
constexpr const char* CONTAINER_NAME = "kuksa-bridge-test";
constexpr const char* KUKSA_PORT = "55557";  // Different port to avoid conflicts

// Helper to run shell commands
int run_cmd(const std::string& cmd) {
    return std::system(cmd.c_str());
}

// Helper to check if port is open
bool is_port_open(const std::string& port) {
    std::string cmd = "nc -z localhost " + port + " 2>/dev/null";
    return run_cmd(cmd) == 0;
}

}  // namespace

class KuksaDdsBridgeReconnectTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        google::InitGoogleLogging("kuksa_dds_bridge_test");
        FLAGS_logtostderr = true;

        // Check Docker availability
        if (run_cmd("docker --version > /dev/null 2>&1") != 0) {
            GTEST_SKIP() << "Docker is not available. Skipping tests.";
        }

        // Create VSS config for tests
        CreateVSSConfig();
    }

    static void TearDownTestSuite() {
        StopContainer();
        CleanupVSSConfig();
    }

    void SetUp() override {
        // Ensure container is stopped before each test
        StopContainer();
        // Give time for ports to be released
        std::this_thread::sleep_for(500ms);
    }

    void TearDown() override {
        StopContainer();
        std::this_thread::sleep_for(200ms);
    }

    static bool StartContainer() {
        LOG(INFO) << "Starting KUKSA databroker container...";

        // Get absolute path to VSS config
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) == nullptr) {
            LOG(ERROR) << "Failed to get current directory";
            return false;
        }
        std::string vss_path = std::string(cwd) + "/vss_bridge_test.json";

        std::stringstream cmd;
        cmd << "docker run -d --rm "
            << "--name " << CONTAINER_NAME << " "
            << "-p " << KUKSA_PORT << ":55555 "
            << "-v " << vss_path << ":/vss/vss_test.json:ro "
            << KUKSA_IMAGE << " "
            << "--vss /vss/vss_test.json "
            << "2>/dev/null";

        if (run_cmd(cmd.str()) != 0) {
            LOG(ERROR) << "Failed to start Docker container";
            return false;
        }

        // Wait for container to be ready
        LOG(INFO) << "Waiting for KUKSA to be ready...";
        for (int i = 0; i < 30; ++i) {
            std::this_thread::sleep_for(1s);

            // Check if container is running
            std::string check = "docker ps -q -f name=" + std::string(CONTAINER_NAME) + " | grep -q .";
            if (run_cmd(check) != 0) {
                LOG(ERROR) << "Container stopped unexpectedly";
                return false;
            }

            if (is_port_open(KUKSA_PORT)) {
                LOG(INFO) << "KUKSA is ready!";
                return true;
            }
        }

        LOG(ERROR) << "Timeout waiting for KUKSA to be ready";
        StopContainer();
        return false;
    }

    static void StopContainer() {
        LOG(INFO) << "Stopping KUKSA container...";
        run_cmd("docker stop " + std::string(CONTAINER_NAME) + " 2>/dev/null");
        run_cmd("docker rm -f " + std::string(CONTAINER_NAME) + " 2>/dev/null");
        std::this_thread::sleep_for(500ms);
    }

    static void CreateVSSConfig() {
        LOG(INFO) << "Creating VSS test configuration...";

        std::ofstream vss_file("vss_bridge_test.json");
        vss_file << R"({
  "Vehicle": {
    "type": "branch",
    "description": "High-level vehicle data",
    "children": {
      "Speed": {
        "type": "sensor",
        "datatype": "float",
        "description": "Vehicle speed"
      },
      "Private": {
        "type": "branch",
        "description": "Private test signals",
        "children": {
          "Test": {
            "type": "branch",
            "description": "Test signals",
            "children": {
              "Sensor1": {
                "type": "sensor",
                "datatype": "float",
                "description": "Test sensor 1"
              },
              "Actuator1": {
                "type": "actuator",
                "datatype": "int32",
                "description": "Test actuator 1"
              }
            }
          }
        }
      }
    }
  }
})";
        vss_file.close();
    }

    static void CleanupVSSConfig() {
        std::remove("vss_bridge_test.json");
    }

    std::string getKuksaAddress() const {
        return std::string("localhost:") + KUKSA_PORT;
    }
};

// Test 1: Bridge fails to initialize when KUKSA is not available
TEST_F(KuksaDdsBridgeReconnectTest, FailsWhenKuksaUnavailable) {
    LOG(INFO) << "Testing bridge initialization failure when KUKSA unavailable";

    // Ensure KUKSA is not running
    ASSERT_FALSE(is_port_open(KUKSA_PORT)) << "KUKSA should not be running";

    bridge::BridgeConfig config;
    config.kuksa_address = getKuksaAddress();
    config.signal_pattern = "Vehicle";

    auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);

    // Should fail to initialize since KUKSA is not available
    EXPECT_FALSE(bridge->initialize()) << "Bridge should fail to initialize without KUKSA";
}

// Test 2: Bridge successfully initializes when KUKSA is available
TEST_F(KuksaDdsBridgeReconnectTest, SucceedsWhenKuksaAvailable) {
    LOG(INFO) << "Testing bridge initialization success when KUKSA available";

    // Start KUKSA
    ASSERT_TRUE(StartContainer()) << "Failed to start KUKSA container";
    ASSERT_TRUE(is_port_open(KUKSA_PORT)) << "KUKSA port should be open";

    bridge::BridgeConfig config;
    config.kuksa_address = getKuksaAddress();
    config.signal_pattern = "Vehicle";

    auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);

    // Should succeed to initialize since KUKSA is available
    EXPECT_TRUE(bridge->initialize()) << "Bridge should initialize with KUKSA running";

    // Start the bridge
    EXPECT_TRUE(bridge->start()) << "Bridge should start successfully";

    // Let it run briefly
    std::this_thread::sleep_for(500ms);

    // Stop cleanly
    bridge->stop();
}

// Test 3: Bridge can reconnect after KUKSA restart
TEST_F(KuksaDdsBridgeReconnectTest, ReconnectsAfterKuksaRestart) {
    LOG(INFO) << "Testing bridge reconnection after KUKSA restart";

    // Start KUKSA first
    ASSERT_TRUE(StartContainer()) << "Failed to start KUKSA container";
    ASSERT_TRUE(is_port_open(KUKSA_PORT)) << "KUKSA port should be open";

    bridge::BridgeConfig config;
    config.kuksa_address = getKuksaAddress();
    config.signal_pattern = "Vehicle";

    // First connection
    {
        auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);
        ASSERT_TRUE(bridge->initialize()) << "First initialization should succeed";
        ASSERT_TRUE(bridge->start()) << "First start should succeed";
        std::this_thread::sleep_for(500ms);
        bridge->stop();
    }

    // Stop KUKSA
    LOG(INFO) << "Stopping KUKSA...";
    StopContainer();
    std::this_thread::sleep_for(1s);
    ASSERT_FALSE(is_port_open(KUKSA_PORT)) << "KUKSA should be stopped";

    // Bridge should fail now
    {
        auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);
        EXPECT_FALSE(bridge->initialize()) << "Should fail without KUKSA";
    }

    // Restart KUKSA
    LOG(INFO) << "Restarting KUKSA...";
    ASSERT_TRUE(StartContainer()) << "Failed to restart KUKSA container";
    ASSERT_TRUE(is_port_open(KUKSA_PORT)) << "KUKSA port should be open again";

    // Bridge should succeed again
    {
        auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);
        EXPECT_TRUE(bridge->initialize()) << "Should succeed with KUKSA restarted";
        EXPECT_TRUE(bridge->start()) << "Should start with KUKSA restarted";
        std::this_thread::sleep_for(500ms);
        bridge->stop();
    }
}

// Test 4: Multiple bridge instances can initialize sequentially
// Note: KUKSA needs time to release actuator ownership between connections
TEST_F(KuksaDdsBridgeReconnectTest, MultipleSequentialConnections) {
    LOG(INFO) << "Testing multiple sequential bridge connections";

    ASSERT_TRUE(StartContainer()) << "Failed to start KUKSA container";

    bridge::BridgeConfig config;
    config.kuksa_address = getKuksaAddress();
    config.signal_pattern = "Vehicle";

    // Create and destroy multiple bridge instances
    // KUKSA needs ~2s to release actuator ownership after client disconnect
    for (int i = 0; i < 3; ++i) {
        LOG(INFO) << "Connection iteration " << (i + 1);
        auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);
        ASSERT_TRUE(bridge->initialize()) << "Iteration " << i << " init failed";
        ASSERT_TRUE(bridge->start()) << "Iteration " << i << " start failed";
        std::this_thread::sleep_for(200ms);
        bridge->stop();
        // Wait for KUKSA to release actuator ownership
        std::this_thread::sleep_for(2500ms);
    }
}

// Test 5: Bridge stats are properly tracked
TEST_F(KuksaDdsBridgeReconnectTest, StatsTracking) {
    LOG(INFO) << "Testing bridge statistics tracking";

    ASSERT_TRUE(StartContainer()) << "Failed to start KUKSA container";

    bridge::BridgeConfig config;
    config.kuksa_address = getKuksaAddress();
    config.signal_pattern = "Vehicle";

    auto bridge = std::make_unique<bridge::KuksaDdsBridge>(config);
    ASSERT_TRUE(bridge->initialize());
    ASSERT_TRUE(bridge->start());

    // Get initial stats
    auto stats = bridge->stats();
    EXPECT_EQ(stats.dds_signals_received, 0u);
    EXPECT_EQ(stats.kuksa_signals_published, 0u);

    std::this_thread::sleep_for(500ms);
    bridge->stop();
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
