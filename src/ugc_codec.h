/**
 * Dewpoint Advance UGC codec
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace DewpointUgc
{
enum class Encoding {
    Lz4,
    LegacyUncompressed,
};

size_t maxCompressedSize(size_t uncompressedSize);

bool compress(
    const uint8_t* data,
    size_t size,
    size_t uncompressedSizeLimit,
    std::vector<uint8_t>* output,
    std::string* error);

bool extract(
    const uint8_t* data,
    size_t size,
    size_t uncompressedSizeLimit,
    std::vector<uint8_t>* output,
    Encoding* encoding,
    std::string* error);
} // namespace DewpointUgc
