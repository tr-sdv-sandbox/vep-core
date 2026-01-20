// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#include "can_encoder.hpp"

#include <gtest/gtest.h>
#include <cstring>

namespace vep {
namespace {

class CANEncoderTest : public ::testing::Test {
protected:
    void SetUp() override {
        encoder_ = std::make_unique<CANEncoder>();
    }

    // Helper to create a numeric VSS value
    vep_VssValue make_int8_value(int8_t val) {
        vep_VssValue v{};
        v.type = vep_VSS_VALUE_TYPE_INT8;
        v.int8_value = static_cast<uint8_t>(val);
        return v;
    }

    vep_VssValue make_int32_value(int32_t val) {
        vep_VssValue v{};
        v.type = vep_VSS_VALUE_TYPE_INT32;
        v.int32_value = val;
        return v;
    }

    vep_VssValue make_double_value(double val) {
        vep_VssValue v{};
        v.type = vep_VSS_VALUE_TYPE_DOUBLE;
        v.double_value = val;
        return v;
    }

    vep_VssValue make_string_value(const char* str) {
        vep_VssValue v{};
        v.type = vep_VSS_VALUE_TYPE_STRING;
        v.string_value = const_cast<char*>(str);
        return v;
    }

    std::unique_ptr<CANEncoder> encoder_;
};

TEST_F(CANEncoderTest, EncodeSimpleInt8) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 8;
    mapping.offset = 0;
    mapping.factor = 1;

    std::vector<uint8_t> frame_data(8, 0);
    auto value = make_int8_value(42);

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    EXPECT_EQ(frame_data[0], 42);
}

TEST_F(CANEncoderTest, EncodeWithOffsetAndFactor) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 8;
    mapping.offset = 10;  // vss_value = raw * 5 + 10
    mapping.factor = 5;   // raw = (vss_value - 10) / 5

    std::vector<uint8_t> frame_data(8, 0);
    // vss_value = 35 -> raw = (35 - 10) / 5 = 5
    auto value = make_int32_value(35);

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    EXPECT_EQ(frame_data[0], 5);
}

TEST_F(CANEncoderTest, EncodeAtBitOffset) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 4;  // Start at bit 4
    mapping.bit_length = 4; // 4 bits
    mapping.offset = 0;
    mapping.factor = 1;

    std::vector<uint8_t> frame_data(8, 0);
    auto value = make_int8_value(0x0F);  // 15 decimal, 1111 binary

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    // Value 15 (1111 binary) at bit position 4 -> byte 0 should be 0xF0
    EXPECT_EQ(frame_data[0], 0xF0);
}

TEST_F(CANEncoderTest, EncodeSpanningBytes) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 4;   // Start at bit 4
    mapping.bit_length = 12; // 12 bits spanning bytes 0 and 1
    mapping.offset = 0;
    mapping.factor = 1;

    std::vector<uint8_t> frame_data(8, 0);
    // Value 0xABC (2748 decimal)
    auto value = make_int32_value(0xABC);

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    // Bits 0-3 of value (0xC = 1100) go to bits 4-7 of byte 0 -> byte 0 = 0xC0
    // Bits 4-11 of value (0xAB = 10101011) go to bits 0-7 of byte 1 -> byte 1 = 0xAB
    EXPECT_EQ(frame_data[0], 0xC0);
    EXPECT_EQ(frame_data[1], 0xAB);
}

TEST_F(CANEncoderTest, EncodeStringEnum) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Mode";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 2;
    mapping.value_mapping = {{"UP", 0}, {"MIDDLE", 1}, {"DOWN", 2}};

    std::vector<uint8_t> frame_data(8, 0);
    auto value = make_string_value("MIDDLE");

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    EXPECT_EQ(frame_data[0], 1);
}

TEST_F(CANEncoderTest, EncodeUnknownStringEnum) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Mode";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 2;
    mapping.value_mapping = {{"UP", 0}, {"DOWN", 1}};

    std::vector<uint8_t> frame_data(8, 0);
    auto value = make_string_value("INVALID");

    EXPECT_FALSE(encoder_->encode_signal(mapping, value, frame_data));
}

TEST_F(CANEncoderTest, ClampToMaxBitValue) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 4;  // Max value is 15
    mapping.offset = 0;
    mapping.factor = 1;

    std::vector<uint8_t> frame_data(8, 0);
    auto value = make_int32_value(100);  // Way over 15

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    EXPECT_EQ(frame_data[0], 15);  // Clamped to max
}

TEST_F(CANEncoderTest, FrameDataAutoResize) {
    CANSignalMapping mapping;
    mapping.vss_path = "Test.Signal";
    mapping.can_id = 0x100;
    mapping.message_length = 8;
    mapping.bit_start = 0;
    mapping.bit_length = 8;
    mapping.offset = 0;
    mapping.factor = 1;

    std::vector<uint8_t> frame_data;  // Empty
    auto value = make_int8_value(42);

    ASSERT_TRUE(encoder_->encode_signal(mapping, value, frame_data));
    EXPECT_GE(frame_data.size(), 8u);
    EXPECT_EQ(frame_data[0], 42);
}

TEST_F(CANEncoderTest, GetNumericValueFromDouble) {
    auto value = make_double_value(123.456);
    double out;
    ASSERT_TRUE(CANEncoder::get_numeric_value(value, out));
    EXPECT_DOUBLE_EQ(out, 123.456);
}

TEST_F(CANEncoderTest, GetStringValue) {
    auto value = make_string_value("test_string");
    std::string out;
    ASSERT_TRUE(CANEncoder::get_string_value(value, out));
    EXPECT_EQ(out, "test_string");
}

TEST_F(CANEncoderTest, EncodeMultipleSignalsSameFrame) {
    // Two signals in the same CAN frame at different bit positions
    CANSignalMapping mapping1;
    mapping1.vss_path = "Test.Signal1";
    mapping1.can_id = 0x100;
    mapping1.message_length = 8;
    mapping1.bit_start = 0;
    mapping1.bit_length = 8;
    mapping1.offset = 0;
    mapping1.factor = 1;

    CANSignalMapping mapping2;
    mapping2.vss_path = "Test.Signal2";
    mapping2.can_id = 0x100;
    mapping2.message_length = 8;
    mapping2.bit_start = 8;  // Byte 1
    mapping2.bit_length = 8;
    mapping2.offset = 0;
    mapping2.factor = 1;

    std::vector<uint8_t> frame_data(8, 0);

    auto value1 = make_int8_value(0xAA);
    auto value2 = make_int8_value(0xBB);

    ASSERT_TRUE(encoder_->encode_signal(mapping1, value1, frame_data));
    ASSERT_TRUE(encoder_->encode_signal(mapping2, value2, frame_data));

    EXPECT_EQ(frame_data[0], 0xAA);
    EXPECT_EQ(frame_data[1], 0xBB);
}

}  // namespace
}  // namespace vep
