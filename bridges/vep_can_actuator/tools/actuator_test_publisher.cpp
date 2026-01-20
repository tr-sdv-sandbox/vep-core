// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0
//
// actuator_test_publisher - Test tool to publish DDS actuator signals
//
// Usage:
//   actuator_test_publisher --path "Vehicle.Cabin.HVAC.IsAirConditioningActive" --value 50

#include "common/dds_wrapper.hpp"
#include "common/qos_profiles.hpp"
#include "vss-signal.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

DEFINE_string(path, "", "VSS actuator path (e.g., Vehicle.Cabin.HVAC.IsAirConditioningActive)");
DEFINE_string(value, "", "Value to set (numeric or string)");
DEFINE_string(topic, "rt/vss/actuators/target", "DDS topic to publish to");
DEFINE_int32(count, 1, "Number of times to publish (0 = infinite)");
DEFINE_int32(interval_ms, 1000, "Interval between publishes in ms");

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::INFO);
    FLAGS_colorlogtostderr = true;

    gflags::SetUsageMessage(
        "Test publisher for DDS actuator signals\n\n"
        "Examples:\n"
        "  actuator_test_publisher --path Vehicle.Cabin.HVAC.IsAirConditioningActive --value 50\n"
        "  actuator_test_publisher --path Vehicle.Cabin.HVAC.AirDistribution --value MIDDLE\n"
        "  actuator_test_publisher --path Vehicle.Cabin.HVAC.IsAirConditioningActive --value 50 --count 10");
    gflags::ParseCommandLineFlags(&argc, &argv, true);

    if (FLAGS_path.empty() || FLAGS_value.empty()) {
        LOG(ERROR) << "Missing required flags: --path and --value";
        return 1;
    }

    try {
        // Create DDS participant and writer
        dds::Participant participant(DDS_DOMAIN_DEFAULT);
        auto qos = dds::qos_profiles::reliable_standard(100);
        dds::Topic topic(participant, &vep_VssSignal_desc, FLAGS_topic, qos.get());
        dds::Writer writer(participant, topic, qos.get());

        LOG(INFO) << "Publishing to topic: " << FLAGS_topic;
        LOG(INFO) << "  Path: " << FLAGS_path;
        LOG(INFO) << "  Value: " << FLAGS_value;

        // Static strings for DDS message (must remain valid during write)
        // All string pointers in DDS structs must be valid (not null)
        std::string source_id = "actuator_test_publisher";
        std::string correlation_id = "";
        std::string empty_string = "";

        int count = 0;
        while (FLAGS_count == 0 || count < FLAGS_count) {
            // Build the signal message - use memset to properly zero all nested pointers
            vep_VssSignal msg;
            memset(&msg, 0, sizeof(msg));

            msg.path = const_cast<char*>(FLAGS_path.c_str());
            msg.quality = vep_VSS_QUALITY_VALID;

            // Fill in header - all string pointers must be valid
            msg.header.source_id = const_cast<char*>(source_id.c_str());
            msg.header.correlation_id = const_cast<char*>(correlation_id.c_str());
            auto now = std::chrono::system_clock::now();
            msg.header.timestamp_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now.time_since_epoch()).count();
            msg.header.seq_num = count;

            // Initialize all string pointers in value struct (DDS requires valid pointers)
            msg.value.string_value = const_cast<char*>(empty_string.c_str());
            msg.value.struct_value.type_name = const_cast<char*>(empty_string.c_str());

            // Try to parse value as numeric first
            bool is_numeric = false;
            try {
                double numeric_val = std::stod(FLAGS_value);
                is_numeric = true;

                // Check if it's an integer
                if (numeric_val == static_cast<int64_t>(numeric_val)) {
                    msg.value.type = vep_VSS_VALUE_TYPE_INT32;
                    msg.value.int32_value = static_cast<int32_t>(numeric_val);
                } else {
                    msg.value.type = vep_VSS_VALUE_TYPE_DOUBLE;
                    msg.value.double_value = numeric_val;
                }
            } catch (...) {
                // Not a number, treat as string
                is_numeric = false;
            }

            if (!is_numeric) {
                msg.value.type = vep_VSS_VALUE_TYPE_STRING;
                msg.value.string_value = const_cast<char*>(FLAGS_value.c_str());
            }

            // Publish (write takes a reference, internally passes pointer to dds_write)
            writer.write(msg);
            count++;

            LOG(INFO) << "Published [" << count << "]: " << FLAGS_path << " = " << FLAGS_value;

            if (FLAGS_count == 0 || count < FLAGS_count) {
                std::this_thread::sleep_for(std::chrono::milliseconds(FLAGS_interval_ms));
            }
        }

        LOG(INFO) << "Done. Published " << count << " message(s).";

    } catch (const dds::Error& e) {
        LOG(FATAL) << "DDS error: " << e.what();
        return 1;
    }

    gflags::ShutDownCommandLineFlags();
    google::ShutdownGoogleLogging();
    return 0;
}
