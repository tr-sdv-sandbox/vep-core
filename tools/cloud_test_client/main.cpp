// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

/// @file main.cpp
/// @brief Cloud Test Client - simulates cloud side for transport testing
///
/// This tool connects to MQTT as the "cloud" side:
/// - Publishes to c2v/{vehicle_id}/{content_id} (commands to vehicle)
/// - Subscribes to v2c/{vehicle_id}/{content_id} (telemetry from vehicle)
///
/// Usage:
///   cloud_test_client --vehicle-id VIN123 --broker localhost
///   cloud_test_client --vehicle-id VIN123 --send 1 --data "hello"
///   cloud_test_client --vehicle-id VIN123 --receive 1

#include <glog/logging.h>
#include <mosquitto.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_running{true};

void signal_handler(int) {
    g_running = false;
}

struct Config {
    std::string broker_host = "localhost";
    int broker_port = 1883;
    std::string vehicle_id;
    std::string v2c_prefix = "v2c";
    std::string c2v_prefix = "c2v";

    // Mode
    bool send_mode = false;
    bool receive_mode = false;
    bool interactive_mode = false;

    uint32_t content_id = 1;
    std::string data;  // Data to send (hex or string)
    bool hex_input = false;
};

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [OPTIONS]\n"
              << "\n"
              << "Cloud Test Client - simulates cloud side for transport testing\n"
              << "\n"
              << "Options:\n"
              << "  --broker HOST      MQTT broker host (default: localhost)\n"
              << "  --port PORT        MQTT broker port (default: 1883)\n"
              << "  --vehicle-id ID    Vehicle identifier (required)\n"
              << "  --send ID          Send mode: publish to c2v/{vehicle_id}/{ID}\n"
              << "  --receive ID       Receive mode: subscribe to v2c/{vehicle_id}/{ID}\n"
              << "  --data DATA        Data to send (string)\n"
              << "  --hex DATA         Data to send (hex string, e.g., 'deadbeef')\n"
              << "  --interactive      Interactive mode: send/receive continuously\n"
              << "  --help             Show this help\n"
              << "\n"
              << "Examples:\n"
              << "  # Send string data to vehicle\n"
              << "  " << prog << " --vehicle-id VIN123 --send 1 --data 'hello world'\n"
              << "\n"
              << "  # Send binary data to vehicle\n"
              << "  " << prog << " --vehicle-id VIN123 --send 1 --hex 'deadbeef01020304'\n"
              << "\n"
              << "  # Receive telemetry from vehicle\n"
              << "  " << prog << " --vehicle-id VIN123 --receive 1\n"
              << "\n"
              << "  # Interactive mode (both directions)\n"
              << "  " << prog << " --vehicle-id VIN123 --interactive\n"
              << "\n";
}

std::vector<uint8_t> hex_to_bytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        uint8_t byte = static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::string bytes_to_printable(const uint8_t* data, size_t len) {
    std::ostringstream oss;
    bool all_printable = true;
    for (size_t i = 0; i < len && i < 64; ++i) {
        if (data[i] < 32 || data[i] > 126) {
            all_printable = false;
            break;
        }
    }

    if (all_printable && len < 256) {
        oss << "\"" << std::string(reinterpret_cast<const char*>(data), len) << "\"";
    } else {
        oss << "hex:" << bytes_to_hex(data, std::min(len, size_t(32)));
        if (len > 32) oss << "...";
    }
    return oss.str();
}

Config parse_args(int argc, char* argv[]) {
    Config config;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            exit(0);
        } else if (arg == "--broker" && i + 1 < argc) {
            config.broker_host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            config.broker_port = std::stoi(argv[++i]);
        } else if (arg == "--vehicle-id" && i + 1 < argc) {
            config.vehicle_id = argv[++i];
        } else if (arg == "--send" && i + 1 < argc) {
            config.send_mode = true;
            config.content_id = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--receive" && i + 1 < argc) {
            config.receive_mode = true;
            config.content_id = static_cast<uint32_t>(std::stoul(argv[++i]));
        } else if (arg == "--data" && i + 1 < argc) {
            config.data = argv[++i];
            config.hex_input = false;
        } else if (arg == "--hex" && i + 1 < argc) {
            config.data = argv[++i];
            config.hex_input = true;
        } else if (arg == "--interactive") {
            config.interactive_mode = true;
        } else {
            LOG(WARNING) << "Unknown argument: " << arg;
        }
    }

    if (config.vehicle_id.empty()) {
        LOG(ERROR) << "Missing required --vehicle-id";
        print_usage(argv[0]);
        exit(1);
    }

    return config;
}

// Message callback for received messages
void on_message(struct mosquitto*, void* obj, const struct mosquitto_message* msg) {
    auto* config = static_cast<Config*>(obj);

    if (!msg || !msg->payload || msg->payloadlen == 0) {
        return;
    }

    std::string topic = msg->topic;
    auto* data = static_cast<uint8_t*>(msg->payload);
    size_t len = static_cast<size_t>(msg->payloadlen);

    // Parse content_id from topic
    std::string v2c_prefix = config->v2c_prefix + "/" + config->vehicle_id + "/";
    uint32_t content_id = 0;
    if (topic.find(v2c_prefix) == 0) {
        content_id = static_cast<uint32_t>(std::stoul(topic.substr(v2c_prefix.length())));
    }

    std::cout << "[v2c] content_id=" << content_id
              << " len=" << len
              << " data=" << bytes_to_printable(data, len)
              << std::endl;
}

void on_connect(struct mosquitto*, void* obj, int rc) {
    auto* config = static_cast<Config*>(obj);

    if (rc == 0) {
        LOG(INFO) << "Connected to MQTT broker";

        // In receive or interactive mode, subscribe to v2c topics
        if (config->receive_mode || config->interactive_mode) {
            std::string topic = config->v2c_prefix + "/" + config->vehicle_id + "/+";
            LOG(INFO) << "Subscribing to: " << topic;
        }
    } else {
        LOG(ERROR) << "MQTT connection failed: " << mosquitto_connack_string(rc);
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = true;

    Config config = parse_args(argc, argv);

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    mosquitto_lib_init();

    struct mosquitto* mosq = mosquitto_new("cloud_test_client", true, &config);
    if (!mosq) {
        LOG(ERROR) << "Failed to create mosquitto client";
        return 1;
    }

    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    int rc = mosquitto_connect(mosq, config.broker_host.c_str(), config.broker_port, 60);
    if (rc != MOSQ_ERR_SUCCESS) {
        LOG(ERROR) << "MQTT connect failed: " << mosquitto_strerror(rc);
        mosquitto_destroy(mosq);
        mosquitto_lib_cleanup();
        return 1;
    }

    LOG(INFO) << "Cloud Test Client connected to " << config.broker_host << ":" << config.broker_port;
    LOG(INFO) << "Vehicle ID: " << config.vehicle_id;

    // Subscribe to v2c topics (telemetry from vehicle)
    if (config.receive_mode || config.interactive_mode) {
        std::string topic = config.v2c_prefix + "/" + config.vehicle_id + "/+";
        rc = mosquitto_subscribe(mosq, nullptr, topic.c_str(), 1);
        if (rc != MOSQ_ERR_SUCCESS) {
            LOG(ERROR) << "Subscribe failed: " << mosquitto_strerror(rc);
        } else {
            LOG(INFO) << "Subscribed to v2c: " << topic;
        }
    }

    // Start network loop in background
    mosquitto_loop_start(mosq);

    // Send mode: publish once and exit
    if (config.send_mode && !config.data.empty()) {
        std::vector<uint8_t> payload;
        if (config.hex_input) {
            payload = hex_to_bytes(config.data);
        } else {
            payload.assign(config.data.begin(), config.data.end());
        }

        std::string topic = config.c2v_prefix + "/" + config.vehicle_id + "/" + std::to_string(config.content_id);

        rc = mosquitto_publish(mosq, nullptr, topic.c_str(),
                               static_cast<int>(payload.size()),
                               payload.data(), 1, false);

        if (rc == MOSQ_ERR_SUCCESS) {
            LOG(INFO) << "Sent to " << topic << ": " << bytes_to_printable(payload.data(), payload.size());
        } else {
            LOG(ERROR) << "Publish failed: " << mosquitto_strerror(rc);
        }

        // Wait a bit for message to be sent
        std::this_thread::sleep_for(std::chrono::milliseconds(500));

        if (!config.receive_mode && !config.interactive_mode) {
            g_running = false;
        }
    }

    // Interactive mode or receive mode: run until interrupted
    if (config.receive_mode || config.interactive_mode) {
        LOG(INFO) << "Listening for messages. Press Ctrl+C to exit.";

        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    // Cleanup
    mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    LOG(INFO) << "Cloud Test Client stopped.";
    return 0;
}
