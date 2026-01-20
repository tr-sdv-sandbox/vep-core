// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// kuksa_actuator_sender - Send actuator commands via KUKSA v2 Actuate API
//
// This tool sends actuator commands using the KUKSA v2 API which properly
// routes to registered actuator providers (like kuksa_dds_bridge).
//
// Usage:
//   kuksa_actuator_sender --kuksa=localhost:55555 \
//       --path="Vehicle.Cabin.HVAC.IsAirConditioningActive" --value=50

#include <kuksa_cpp/client.hpp>
#include <kuksa_cpp/resolver.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>

DEFINE_string(kuksa, "localhost:55555", "KUKSA databroker address");
DEFINE_string(path, "", "VSS actuator path");
DEFINE_string(value, "", "Value to set (numeric or string)");

using namespace kuksa;
using namespace std::chrono_literals;

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    FLAGS_colorlogtostderr = true;

    gflags::SetUsageMessage(
        "Send actuator commands via KUKSA v2 Actuate API\n\n"
        "Examples:\n"
        "  kuksa_actuator_sender --kuksa=localhost:55555 "
        "--path=Vehicle.Cabin.HVAC.IsAirConditioningActive --value=50\n"
        "  kuksa_actuator_sender --kuksa=localhost:55555 "
        "--path=Vehicle.Cabin.HVAC.AirDistribution --value=MIDDLE");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_path.empty() || FLAGS_value.empty()) {
        LOG(ERROR) << "Missing required flags: --path and --value";
        return 1;
    }

    LOG(INFO) << "Connecting to KUKSA at " << FLAGS_kuksa;

    // Create resolver
    auto resolver_result = Resolver::create(FLAGS_kuksa);
    if (!resolver_result.ok()) {
        LOG(ERROR) << "Failed to create resolver: " << resolver_result.status();
        return 1;
    }
    auto resolver = std::move(*resolver_result);

    // Create client
    auto client_result = Client::create(FLAGS_kuksa);
    if (!client_result.ok()) {
        LOG(ERROR) << "Failed to create client: " << client_result.status();
        return 1;
    }
    auto client = std::move(*client_result);

    // Resolve handle dynamically
    auto handle_result = resolver->get_dynamic(FLAGS_path);
    if (!handle_result.ok()) {
        LOG(ERROR) << "Failed to resolve path '" << FLAGS_path << "': "
                   << handle_result.status();
        return 1;
    }
    auto handle_ptr = *handle_result;
    const auto& handle = *handle_ptr;  // Dereference shared_ptr

    LOG(INFO) << "Resolved: " << FLAGS_path;
    LOG(INFO) << "  Signal ID: " << handle.id();
    LOG(INFO) << "  Type: " << static_cast<int>(handle.type());
    LOG(INFO) << "  Class: " << (handle.signal_class() == SignalClass::ACTUATOR
                                 ? "ACTUATOR" : "OTHER");

    // Parse value based on type
    vss::types::Value value;
    auto value_type = handle.type();

    try {
        switch (value_type) {
            case vss::types::ValueType::BOOL:
                value = (FLAGS_value == "true" || FLAGS_value == "1");
                break;
            case vss::types::ValueType::INT8:
                value = static_cast<int8_t>(std::stoi(FLAGS_value));
                break;
            case vss::types::ValueType::INT16:
                value = static_cast<int16_t>(std::stoi(FLAGS_value));
                break;
            case vss::types::ValueType::INT32:
                value = static_cast<int32_t>(std::stoi(FLAGS_value));
                break;
            case vss::types::ValueType::INT64:
                value = static_cast<int64_t>(std::stoll(FLAGS_value));
                break;
            case vss::types::ValueType::UINT8:
                value = static_cast<uint8_t>(std::stoul(FLAGS_value));
                break;
            case vss::types::ValueType::UINT16:
                value = static_cast<uint16_t>(std::stoul(FLAGS_value));
                break;
            case vss::types::ValueType::UINT32:
                value = static_cast<uint32_t>(std::stoul(FLAGS_value));
                break;
            case vss::types::ValueType::UINT64:
                value = static_cast<uint64_t>(std::stoull(FLAGS_value));
                break;
            case vss::types::ValueType::FLOAT:
                value = static_cast<float>(std::stof(FLAGS_value));
                break;
            case vss::types::ValueType::DOUBLE:
                value = std::stod(FLAGS_value);
                break;
            case vss::types::ValueType::STRING:
                value = FLAGS_value;
                break;
            default:
                LOG(ERROR) << "Unsupported value type";
                return 1;
        }
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to parse value: " << e.what();
        return 1;
    }

    // Send the actuate command using set() which auto-routes to Actuate RPC
    LOG(INFO) << "Sending actuator command: " << FLAGS_path << " = " << FLAGS_value;

    // Wrap value in DynamicQualifiedValue with VALID quality
    vss::types::DynamicQualifiedValue qvalue(value, vss::types::SignalQuality::VALID);

    auto status = client->set(handle, qvalue);
    if (status.ok()) {
        LOG(INFO) << "SUCCESS: Actuator command sent!";
    } else {
        LOG(ERROR) << "FAILED: " << status;
        return 1;
    }

    gflags::ShutDownCommandLineFlags();
    google::ShutdownGoogleLogging();
    return 0;
}
