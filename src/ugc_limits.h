/**
 * Dewpoint Advance UGC limits
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#pragma once

#include <cstdint>

namespace DewpointUgc
{
constexpr uint32_t DEFAULT_SIZE_LIMIT = 1024u * 1024u;
constexpr uint32_t MIN_SIZE_LIMIT = sizeof(uint32_t);
// Leave room for the LZ4 frame overhead in Steam's signed 32-bit file size.
constexpr uint32_t MAX_SIZE_LIMIT = 0x7FF00000u;

constexpr bool isValidSizeLimit(uint32_t size)
{
    return size >= MIN_SIZE_LIMIT &&
           size <= MAX_SIZE_LIMIT &&
           (size % sizeof(uint32_t)) == 0;
}
} // namespace DewpointUgc
