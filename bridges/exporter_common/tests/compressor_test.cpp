// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "compressor.hpp"

#include <gtest/gtest.h>
#include <algorithm>
#include <random>

namespace vep::exporter::test {

class CompressorTest : public ::testing::Test {
protected:
    // Generate random data
    std::vector<uint8_t> generate_random_data(size_t size) {
        std::vector<uint8_t> data(size);
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> distrib(0, 255);
        for (auto& byte : data) {
            byte = static_cast<uint8_t>(distrib(gen));
        }
        return data;
    }

    // Generate compressible data (repeating pattern)
    std::vector<uint8_t> generate_compressible_data(size_t size) {
        std::vector<uint8_t> data(size);
        for (size_t i = 0; i < size; ++i) {
            data[i] = static_cast<uint8_t>(i % 10);  // Repeating pattern
        }
        return data;
    }
};

// =============================================================================
// CompressorType Conversion Tests
// =============================================================================

TEST_F(CompressorTest, ToString) {
    EXPECT_STREQ(to_string(CompressorType::ZSTD), "zstd");
    EXPECT_STREQ(to_string(CompressorType::NONE), "none");
}

TEST_F(CompressorTest, FromString_Valid) {
    EXPECT_EQ(compressor_type_from_string("zstd"), CompressorType::ZSTD);
    EXPECT_EQ(compressor_type_from_string("none"), CompressorType::NONE);
}

TEST_F(CompressorTest, FromString_CaseInsensitive) {
    EXPECT_EQ(compressor_type_from_string("ZSTD"), CompressorType::ZSTD);
    EXPECT_EQ(compressor_type_from_string("Zstd"), CompressorType::ZSTD);
    EXPECT_EQ(compressor_type_from_string("NONE"), CompressorType::NONE);
    EXPECT_EQ(compressor_type_from_string("None"), CompressorType::NONE);
}

TEST_F(CompressorTest, FromString_Invalid) {
    EXPECT_FALSE(compressor_type_from_string("unknown").has_value());
    EXPECT_FALSE(compressor_type_from_string("gzip").has_value());
    EXPECT_FALSE(compressor_type_from_string("").has_value());
}

// =============================================================================
// ZstdCompressor Tests
// =============================================================================

TEST_F(CompressorTest, ZstdCompressor_Init) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());
    EXPECT_EQ(compressor.type(), CompressorType::ZSTD);
    EXPECT_STREQ(compressor.name(), "zstd");
}

TEST_F(CompressorTest, ZstdCompressor_CompressEmpty) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    std::vector<uint8_t> empty;
    auto compressed = compressor.compress(empty);

    // Empty input should produce minimal output (just zstd header)
    EXPECT_FALSE(compressed.empty());
}

TEST_F(CompressorTest, ZstdCompressor_CompressSmallData) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto compressed = compressor.compress(data);

    EXPECT_FALSE(compressed.empty());
    // Small data may not compress well, but should still produce output
}

TEST_F(CompressorTest, ZstdCompressor_CompressCompressibleData) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    auto data = generate_compressible_data(10000);
    auto compressed = compressor.compress(data);

    EXPECT_FALSE(compressed.empty());
    // Compressible data should compress significantly
    EXPECT_LT(compressed.size(), data.size());
}

TEST_F(CompressorTest, ZstdCompressor_CompressionLevels) {
    auto data = generate_compressible_data(10000);

    // Test different compression levels
    for (int level : {1, 3, 9, 19}) {
        ZstdCompressor compressor(level);
        EXPECT_TRUE(compressor.init());

        auto compressed = compressor.compress(data);
        EXPECT_FALSE(compressed.empty());
        EXPECT_LT(compressed.size(), data.size());
    }
}

TEST_F(CompressorTest, ZstdCompressor_Stats) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    auto data = generate_compressible_data(1000);
    compressor.compress(data);

    auto stats = compressor.stats();
    EXPECT_EQ(stats.bytes_before, 1000);
    EXPECT_GT(stats.bytes_after, 0);
    EXPECT_EQ(stats.operations, 1);
}

TEST_F(CompressorTest, ZstdCompressor_MultipleCompressions) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    for (int i = 0; i < 10; ++i) {
        auto data = generate_compressible_data(1000);
        auto compressed = compressor.compress(data);
        EXPECT_FALSE(compressed.empty());
    }

    auto stats = compressor.stats();
    EXPECT_EQ(stats.bytes_before, 10000);
    EXPECT_EQ(stats.operations, 10);
}

// =============================================================================
// NoCompressor Tests
// =============================================================================

TEST_F(CompressorTest, NoCompressor_Init) {
    NoCompressor compressor;
    EXPECT_TRUE(compressor.init());
    EXPECT_EQ(compressor.type(), CompressorType::NONE);
    EXPECT_STREQ(compressor.name(), "none");
}

TEST_F(CompressorTest, NoCompressor_CompressReturnsIdentical) {
    NoCompressor compressor;
    EXPECT_TRUE(compressor.init());

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto compressed = compressor.compress(data);

    EXPECT_EQ(compressed, data);
}

TEST_F(CompressorTest, NoCompressor_CompressEmpty) {
    NoCompressor compressor;
    EXPECT_TRUE(compressor.init());

    std::vector<uint8_t> empty;
    auto compressed = compressor.compress(empty);

    EXPECT_TRUE(compressed.empty());
}

TEST_F(CompressorTest, NoCompressor_Stats) {
    NoCompressor compressor;
    EXPECT_TRUE(compressor.init());

    std::vector<uint8_t> data(100);
    compressor.compress(data);

    auto stats = compressor.stats();
    EXPECT_EQ(stats.bytes_before, 100);
    EXPECT_EQ(stats.bytes_after, 100);
    EXPECT_EQ(stats.operations, 1);
}

// =============================================================================
// Factory Function Tests
// =============================================================================

TEST_F(CompressorTest, CreateCompressor_Zstd) {
    auto compressor = create_compressor(CompressorType::ZSTD, 3);
    ASSERT_NE(compressor, nullptr);
    EXPECT_EQ(compressor->type(), CompressorType::ZSTD);
}

TEST_F(CompressorTest, CreateCompressor_None) {
    auto compressor = create_compressor(CompressorType::NONE);
    ASSERT_NE(compressor, nullptr);
    EXPECT_EQ(compressor->type(), CompressorType::NONE);
}

// =============================================================================
// CompressionStats Tests
// =============================================================================

TEST_F(CompressorTest, CompressionStats_Ratio) {
    CompressionStats stats;
    stats.bytes_before = 1000;
    stats.bytes_after = 500;

    EXPECT_DOUBLE_EQ(stats.ratio(), 0.5);
}

TEST_F(CompressorTest, CompressionStats_RatioZeroInput) {
    CompressionStats stats;
    stats.bytes_before = 0;
    stats.bytes_after = 0;

    EXPECT_DOUBLE_EQ(stats.ratio(), 0.0);
}

// =============================================================================
// ZstdDecompressor Tests
// =============================================================================

TEST_F(CompressorTest, ZstdDecompressor_Init) {
    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());
    EXPECT_EQ(decompressor.type(), CompressorType::ZSTD);
    EXPECT_STREQ(decompressor.name(), "zstd");
}

TEST_F(CompressorTest, ZstdDecompressor_RoundTrip) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    auto original = generate_compressible_data(10000);
    auto compressed = compressor.compress(original);
    auto decompressed = decompressor.decompress(compressed);

    EXPECT_EQ(decompressed, original);
}

TEST_F(CompressorTest, ZstdDecompressor_RoundTripSmallData) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    std::vector<uint8_t> original = {1, 2, 3, 4, 5};
    auto compressed = compressor.compress(original);
    auto decompressed = decompressor.decompress(compressed);

    EXPECT_EQ(decompressed, original);
}

TEST_F(CompressorTest, ZstdDecompressor_RoundTripEmpty) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    std::vector<uint8_t> empty;
    auto compressed = compressor.compress(empty);
    auto decompressed = decompressor.decompress(compressed);

    EXPECT_TRUE(decompressed.empty());
}

TEST_F(CompressorTest, ZstdDecompressor_InvalidData) {
    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    std::vector<uint8_t> garbage = {0xFF, 0xFE, 0xFD, 0xFC};
    auto result = decompressor.decompress(garbage);

    EXPECT_TRUE(result.empty());
    EXPECT_EQ(decompressor.stats().errors, 1);
}

TEST_F(CompressorTest, ZstdDecompressor_Stats) {
    ZstdCompressor compressor(3);
    EXPECT_TRUE(compressor.init());

    ZstdDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    auto original = generate_compressible_data(1000);
    auto compressed = compressor.compress(original);
    decompressor.decompress(compressed);

    auto stats = decompressor.stats();
    EXPECT_EQ(stats.bytes_before, compressed.size());
    EXPECT_EQ(stats.bytes_after, 1000);
    EXPECT_EQ(stats.operations, 1);
    EXPECT_EQ(stats.errors, 0);
}

// =============================================================================
// NoDecompressor Tests
// =============================================================================

TEST_F(CompressorTest, NoDecompressor_Init) {
    NoDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());
    EXPECT_EQ(decompressor.type(), CompressorType::NONE);
    EXPECT_STREQ(decompressor.name(), "none");
}

TEST_F(CompressorTest, NoDecompressor_Passthrough) {
    NoDecompressor decompressor;
    EXPECT_TRUE(decompressor.init());

    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    auto result = decompressor.decompress(data);

    EXPECT_EQ(result, data);
}

// =============================================================================
// Decompressor Factory Tests
// =============================================================================

TEST_F(CompressorTest, CreateDecompressor_Zstd) {
    auto decompressor = create_decompressor(CompressorType::ZSTD);
    ASSERT_NE(decompressor, nullptr);
    EXPECT_EQ(decompressor->type(), CompressorType::ZSTD);
}

TEST_F(CompressorTest, CreateDecompressor_None) {
    auto decompressor = create_decompressor(CompressorType::NONE);
    ASSERT_NE(decompressor, nullptr);
    EXPECT_EQ(decompressor->type(), CompressorType::NONE);
}

// =============================================================================
// DecompressionStats Tests
// =============================================================================

TEST_F(CompressorTest, DecompressionStats_Ratio) {
    DecompressionStats stats;
    stats.bytes_before = 100;
    stats.bytes_after = 500;

    EXPECT_DOUBLE_EQ(stats.ratio(), 5.0);  // 5x expansion
}

TEST_F(CompressorTest, DecompressionStats_RatioZeroInput) {
    DecompressionStats stats;
    stats.bytes_before = 0;
    stats.bytes_after = 0;

    EXPECT_DOUBLE_EQ(stats.ratio(), 0.0);
}

}  // namespace vep::exporter::test
