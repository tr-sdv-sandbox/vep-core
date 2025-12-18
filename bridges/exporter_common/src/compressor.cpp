// Copyright 2025 COVESA IFEX VDR Integration Contributors
// SPDX-License-Identifier: Apache-2.0

#include "compressor.hpp"

#include <glog/logging.h>
#include <zstd.h>

#include <algorithm>
#include <cctype>

namespace vep::exporter {

const char* to_string(CompressorType type) {
    switch (type) {
        case CompressorType::ZSTD: return "zstd";
        case CompressorType::NONE: return "none";
    }
    return "unknown";
}

std::optional<CompressorType> compressor_type_from_string(const std::string& name) {
    // Convert to lowercase for case-insensitive comparison
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower == "zstd") {
        return CompressorType::ZSTD;
    } else if (lower == "none") {
        return CompressorType::NONE;
    }

    return std::nullopt;
}

ZstdCompressor::ZstdCompressor(int level)
    : level_(level) {
}

ZstdCompressor::~ZstdCompressor() {
    if (ctx_) {
        ZSTD_freeCCtx(ctx_);
        ctx_ = nullptr;
    }
}

bool ZstdCompressor::init() {
    ctx_ = ZSTD_createCCtx();
    if (!ctx_) {
        LOG(ERROR) << "Failed to create ZSTD compression context";
        return false;
    }
    ZSTD_CCtx_setParameter(ctx_, ZSTD_c_compressionLevel, level_);
    return true;
}

std::vector<uint8_t> ZstdCompressor::compress(const std::vector<uint8_t>& data) {
    if (!ctx_) {
        LOG(WARNING) << "ZSTD context not initialized, returning uncompressed";
        return std::vector<uint8_t>(data.begin(), data.end());
    }

    size_t max_size = ZSTD_compressBound(data.size());
    std::vector<uint8_t> compressed(max_size);

    size_t result = ZSTD_compressCCtx(
        ctx_,
        compressed.data(), compressed.size(),
        data.data(), data.size(),
        level_);

    stats_.bytes_before += data.size();
    stats_.operations++;

    if (ZSTD_isError(result)) {
        LOG(WARNING) << "Compression failed: " << ZSTD_getErrorName(result);
        // Fall back to uncompressed
        stats_.bytes_after += data.size();
        return std::vector<uint8_t>(data.begin(), data.end());
    }

    compressed.resize(result);
    stats_.bytes_after += result;
    return compressed;
}

CompressionStats ZstdCompressor::stats() const {
    return stats_;
}

std::unique_ptr<Compressor> create_compressor(CompressorType type, int level) {
    switch (type) {
        case CompressorType::ZSTD: {
            auto comp = std::make_unique<ZstdCompressor>(level);
            if (!comp->init()) {
                return nullptr;
            }
            return comp;
        }
        case CompressorType::NONE:
            return std::make_unique<NoCompressor>();
    }
    return nullptr;
}

// =============================================================================
// Decompression
// =============================================================================

ZstdDecompressor::~ZstdDecompressor() {
    if (ctx_) {
        ZSTD_freeDCtx(ctx_);
        ctx_ = nullptr;
    }
}

bool ZstdDecompressor::init() {
    ctx_ = ZSTD_createDCtx();
    if (!ctx_) {
        LOG(ERROR) << "Failed to create ZSTD decompression context";
        return false;
    }
    return true;
}

std::vector<uint8_t> ZstdDecompressor::decompress(const std::vector<uint8_t>& data) {
    if (!ctx_) {
        LOG(WARNING) << "ZSTD decompression context not initialized";
        stats_.errors++;
        return {};
    }

    if (data.empty()) {
        return {};
    }

    // Get decompressed size from frame header
    unsigned long long decompressed_size = ZSTD_getFrameContentSize(data.data(), data.size());

    if (decompressed_size == ZSTD_CONTENTSIZE_UNKNOWN) {
        // Size unknown, use a reasonable estimate
        decompressed_size = data.size() * 10;
    } else if (decompressed_size == ZSTD_CONTENTSIZE_ERROR) {
        LOG(ERROR) << "Invalid zstd frame";
        stats_.errors++;
        return {};
    }

    std::vector<uint8_t> decompressed(decompressed_size);

    size_t result = ZSTD_decompressDCtx(
        ctx_,
        decompressed.data(), decompressed.size(),
        data.data(), data.size());

    stats_.bytes_before += data.size();
    stats_.operations++;

    if (ZSTD_isError(result)) {
        LOG(ERROR) << "Decompression failed: " << ZSTD_getErrorName(result);
        stats_.errors++;
        return {};
    }

    decompressed.resize(result);
    stats_.bytes_after += result;
    return decompressed;
}

std::unique_ptr<Decompressor> create_decompressor(CompressorType type) {
    switch (type) {
        case CompressorType::ZSTD: {
            auto decomp = std::make_unique<ZstdDecompressor>();
            if (!decomp->init()) {
                return nullptr;
            }
            return decomp;
        }
        case CompressorType::NONE:
            return std::make_unique<NoDecompressor>();
    }
    return nullptr;
}

}  // namespace vep::exporter
