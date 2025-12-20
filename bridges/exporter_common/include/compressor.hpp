// Copyright 2025 Vehicle Edge Platform Contributors
// SPDX-License-Identifier: Apache-2.0

#pragma once

/// @file compressor.hpp
/// @brief Compression/decompression abstraction for exporter pipelines
///
/// Provides pluggable compression strategies (zstd, none, etc.)
/// Used by exporters (compression) and receivers (decompression)

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// Forward declarations
struct ZSTD_CCtx_s;
typedef struct ZSTD_CCtx_s ZSTD_CCtx;
struct ZSTD_DCtx_s;
typedef struct ZSTD_DCtx_s ZSTD_DCtx;

namespace vep::exporter {

/// Supported compressor types
enum class CompressorType {
    NONE,   ///< No compression (passthrough)
    ZSTD    ///< Zstandard compression
};

/// Convert CompressorType to string
/// @return "none" or "zstd"
const char* to_string(CompressorType type);

/// Parse CompressorType from string
/// @param name Compressor name (case-insensitive: "zstd", "ZSTD", "none", "NONE")
/// @return CompressorType if valid, nullopt if unknown
std::optional<CompressorType> compressor_type_from_string(const std::string& name);

/// Compression statistics
struct CompressionStats {
    uint64_t bytes_before = 0;
    uint64_t bytes_after = 0;
    uint64_t operations = 0;

    /// Get compression ratio (0.0 = no data, 0.2 = 80% reduction)
    double ratio() const {
        return bytes_before > 0
            ? static_cast<double>(bytes_after) / bytes_before
            : 0.0;
    }
};

/// Abstract interface for compression
class Compressor {
public:
    virtual ~Compressor() = default;

    /// Initialize the compressor
    /// @return true on success
    virtual bool init() = 0;

    /// Compress data
    /// @param data Input data to compress
    /// @return Compressed data (may be larger if incompressible)
    virtual std::vector<uint8_t> compress(const std::vector<uint8_t>& data) = 0;

    /// Get compression statistics
    virtual CompressionStats stats() const = 0;

    /// Get compressor type
    virtual CompressorType type() const = 0;

    /// Get compressor name for logging
    const char* name() const { return to_string(type()); }
};

/// ZSTD compressor implementation
class ZstdCompressor : public Compressor {
public:
    /// Create a ZSTD compressor
    /// @param level Compression level (1-19, default 3)
    explicit ZstdCompressor(int level = 3);
    ~ZstdCompressor() override;

    ZstdCompressor(const ZstdCompressor&) = delete;
    ZstdCompressor& operator=(const ZstdCompressor&) = delete;

    bool init() override;
    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) override;
    CompressionStats stats() const override;
    CompressorType type() const override { return CompressorType::ZSTD; }

private:
    int level_;
    ZSTD_CCtx* ctx_ = nullptr;
    mutable CompressionStats stats_;
};

/// No-op compressor (passthrough)
class NoCompressor : public Compressor {
public:
    bool init() override { return true; }

    std::vector<uint8_t> compress(const std::vector<uint8_t>& data) override {
        stats_.bytes_before += data.size();
        stats_.bytes_after += data.size();
        stats_.operations++;
        return data;
    }

    CompressionStats stats() const override { return stats_; }
    CompressorType type() const override { return CompressorType::NONE; }

private:
    mutable CompressionStats stats_;
};

/// Factory function to create compressor by type
/// @param type Compressor type
/// @param level Compression level (for ZSTD, 1-19, default 3)
/// @return Compressor instance
std::unique_ptr<Compressor> create_compressor(CompressorType type, int level = 3);

// =============================================================================
// Decompression
// =============================================================================

/// Decompression statistics
struct DecompressionStats {
    uint64_t bytes_before = 0;   ///< Compressed bytes in
    uint64_t bytes_after = 0;    ///< Decompressed bytes out
    uint64_t operations = 0;
    uint64_t errors = 0;

    /// Get expansion ratio (e.g., 5.0 = data expanded 5x)
    double ratio() const {
        return bytes_before > 0
            ? static_cast<double>(bytes_after) / bytes_before
            : 0.0;
    }
};

/// Abstract interface for decompression
class Decompressor {
public:
    virtual ~Decompressor() = default;

    /// Initialize the decompressor
    /// @return true on success
    virtual bool init() = 0;

    /// Decompress data
    /// @param data Compressed input data
    /// @return Decompressed data, or empty vector on error
    virtual std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) = 0;

    /// Get decompression statistics
    virtual DecompressionStats stats() const = 0;

    /// Get decompressor type
    virtual CompressorType type() const = 0;

    /// Get decompressor name for logging
    const char* name() const { return to_string(type()); }
};

/// ZSTD decompressor implementation
class ZstdDecompressor : public Decompressor {
public:
    ZstdDecompressor() = default;
    ~ZstdDecompressor() override;

    ZstdDecompressor(const ZstdDecompressor&) = delete;
    ZstdDecompressor& operator=(const ZstdDecompressor&) = delete;

    bool init() override;
    std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) override;
    DecompressionStats stats() const override { return stats_; }
    CompressorType type() const override { return CompressorType::ZSTD; }

private:
    ZSTD_DCtx* ctx_ = nullptr;
    mutable DecompressionStats stats_;
};

/// No-op decompressor (passthrough)
class NoDecompressor : public Decompressor {
public:
    bool init() override { return true; }

    std::vector<uint8_t> decompress(const std::vector<uint8_t>& data) override {
        stats_.bytes_before += data.size();
        stats_.bytes_after += data.size();
        stats_.operations++;
        return data;
    }

    DecompressionStats stats() const override { return stats_; }
    CompressorType type() const override { return CompressorType::NONE; }

private:
    mutable DecompressionStats stats_;
};

/// Factory function to create decompressor by type
/// @param type Decompressor type (matches CompressorType)
/// @return Decompressor instance
std::unique_ptr<Decompressor> create_decompressor(CompressorType type);

}  // namespace vep::exporter
