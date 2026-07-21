/**
 * Dewpoint Advance UGC codec
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */

#include "ugc_codec.h"

#include "lz4/lz4frame.h"

#include <algorithm>
#include <array>
#include <limits>
#include <memory>

namespace
{
constexpr std::array<uint8_t, 12> ENVELOPE_MAGIC = {
    'D', 'P', 'A', '-', 'U', 'G', 'C', '-', 'L', 'Z', '4', 0x1A,
};
constexpr uint16_t ENVELOPE_VERSION = 1;
constexpr size_t ENVELOPE_SIZE = ENVELOPE_MAGIC.size() + sizeof(uint16_t) + sizeof(uint16_t);

LZ4F_preferences_t compressionPreferences(size_t contentSize)
{
    LZ4F_preferences_t preferences = LZ4F_INIT_PREFERENCES;
    preferences.frameInfo.contentSize = static_cast<unsigned long long>(contentSize);
    preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
    preferences.frameInfo.blockChecksumFlag = LZ4F_blockChecksumEnabled;
    return preferences;
}

bool fail(std::vector<uint8_t>* output, std::string* error, const std::string& message)
{
    if (output) {
        output->clear();
    }
    if (error) {
        *error = message;
    }
    return false;
}

bool hasEnvelopeMagic(const uint8_t* data, size_t size)
{
    return size >= ENVELOPE_MAGIC.size() &&
           std::equal(ENVELOPE_MAGIC.begin(), ENVELOPE_MAGIC.end(), data);
}

uint16_t readU16(const uint8_t* data)
{
    return static_cast<uint16_t>(data[0]) |
           static_cast<uint16_t>(static_cast<uint16_t>(data[1]) << 8);
}

struct DecompressionContextDeleter {
    void operator()(LZ4F_dctx* context) const
    {
        LZ4F_freeDecompressionContext(context);
    }
};
} // namespace

namespace DewpointUgc
{
size_t maxCompressedSize(size_t uncompressedSize)
{
    if (!uncompressedSize) {
        return 0;
    }
    const LZ4F_preferences_t preferences = compressionPreferences(uncompressedSize);
    const size_t frameSize = LZ4F_compressFrameBound(uncompressedSize, &preferences);
    if (LZ4F_isError(frameSize) || frameSize > std::numeric_limits<size_t>::max() - ENVELOPE_SIZE) {
        return 0;
    }
    return ENVELOPE_SIZE + frameSize;
}

bool compress(
    const uint8_t* data,
    size_t size,
    size_t uncompressedSizeLimit,
    std::vector<uint8_t>* output,
    std::string* error)
{
    if (error) {
        error->clear();
    }
    if (!output) {
        return fail(nullptr, error, "output buffer is null");
    }
    output->clear();
    if (!data || !size) {
        return fail(output, error, "UGC data is empty");
    }
    if (size > uncompressedSizeLimit) {
        return fail(output, error, "uncompressed size limit exceeded");
    }

    const LZ4F_preferences_t preferences = compressionPreferences(size);
    const size_t frameCapacity = LZ4F_compressFrameBound(size, &preferences);
    if (LZ4F_isError(frameCapacity)) {
        return fail(output, error, LZ4F_getErrorName(frameCapacity));
    }
    if (frameCapacity > std::numeric_limits<size_t>::max() - ENVELOPE_SIZE) {
        return fail(output, error, "compressed size is too large");
    }
    output->resize(ENVELOPE_SIZE + frameCapacity);
    std::copy(ENVELOPE_MAGIC.begin(), ENVELOPE_MAGIC.end(), output->begin());
    (*output)[ENVELOPE_MAGIC.size()] = static_cast<uint8_t>(ENVELOPE_VERSION);
    (*output)[ENVELOPE_MAGIC.size() + 1] = static_cast<uint8_t>(ENVELOPE_VERSION >> 8);
    (*output)[ENVELOPE_MAGIC.size() + 2] = 0;
    (*output)[ENVELOPE_MAGIC.size() + 3] = 0;
    const size_t compressedSize = LZ4F_compressFrame(
        output->data() + ENVELOPE_SIZE,
        frameCapacity,
        data,
        size,
        &preferences);
    if (LZ4F_isError(compressedSize)) {
        return fail(output, error, LZ4F_getErrorName(compressedSize));
    }
    output->resize(ENVELOPE_SIZE + compressedSize);
    return true;
}

bool extract(
    const uint8_t* data,
    size_t size,
    size_t uncompressedSizeLimit,
    std::vector<uint8_t>* output,
    Encoding* encoding,
    std::string* error)
{
    if (error) {
        error->clear();
    }
    if (!output) {
        return fail(nullptr, error, "output buffer is null");
    }
    output->clear();
    if (!data || !size) {
        return fail(output, error, "UGC data is empty");
    }

    if (!hasEnvelopeMagic(data, size)) {
        if (size > uncompressedSizeLimit) {
            return fail(output, error, "legacy uncompressed size limit exceeded");
        }
        if ((size % sizeof(uint32_t)) != 0) {
            return fail(output, error, "legacy uncompressed UGC is not word-aligned");
        }
        output->assign(data, data + size);
        if (encoding) {
            *encoding = Encoding::LegacyUncompressed;
        }
        return true;
    }

    if (size < ENVELOPE_SIZE) {
        return fail(output, error, "truncated Dewpoint UGC header");
    }
    const uint16_t version = readU16(data + ENVELOPE_MAGIC.size());
    const uint16_t reserved = readU16(data + ENVELOPE_MAGIC.size() + sizeof(uint16_t));
    if (version != ENVELOPE_VERSION || reserved != 0) {
        return fail(output, error, "unsupported Dewpoint UGC header");
    }
    const size_t compressedSizeLimit = maxCompressedSize(uncompressedSizeLimit);
    if (!compressedSizeLimit || size > compressedSizeLimit) {
        return fail(output, error, "compressed size limit exceeded");
    }

    const uint8_t* frameData = data + ENVELOPE_SIZE;
    const size_t frameSize = size - ENVELOPE_SIZE;
    LZ4F_dctx* rawContext = nullptr;
    const LZ4F_errorCode_t createResult =
        LZ4F_createDecompressionContext(&rawContext, LZ4F_VERSION);
    if (LZ4F_isError(createResult)) {
        return fail(output, error, LZ4F_getErrorName(createResult));
    }
    std::unique_ptr<LZ4F_dctx, DecompressionContextDeleter> context(rawContext);

    LZ4F_frameInfo_t frameInfo = LZ4F_INIT_FRAMEINFO;
    size_t headerSize = frameSize;
    const size_t frameInfoResult =
        LZ4F_getFrameInfo(context.get(), &frameInfo, frameData, &headerSize);
    if (LZ4F_isError(frameInfoResult)) {
        return fail(output, error, LZ4F_getErrorName(frameInfoResult));
    }
    if (frameInfo.frameType != LZ4F_frame || !frameInfo.contentSize ||
        frameInfo.contentChecksumFlag != LZ4F_contentChecksumEnabled ||
        frameInfo.blockChecksumFlag != LZ4F_blockChecksumEnabled) {
        return fail(output, error, "unsupported LZ4 frame format");
    }
    if (frameInfo.contentSize > uncompressedSizeLimit ||
        frameInfo.contentSize > std::numeric_limits<size_t>::max()) {
        return fail(output, error, "uncompressed size limit exceeded");
    }

    output->resize(static_cast<size_t>(frameInfo.contentSize));
    size_t sourceOffset = headerSize;
    size_t destinationOffset = 0;
    size_t nextHint = frameInfoResult;
    uint8_t emptyDestination = 0;
    while (nextHint != 0) {
        if (sourceOffset >= frameSize) {
            return fail(output, error, "truncated LZ4 frame");
        }

        size_t sourceChunkSize = frameSize - sourceOffset;
        size_t destinationChunkSize = output->size() - destinationOffset;
        void* destination = destinationChunkSize
            ? static_cast<void*>(output->data() + destinationOffset)
            : static_cast<void*>(&emptyDestination);
        nextHint = LZ4F_decompress(
            context.get(),
            destination,
            &destinationChunkSize,
            frameData + sourceOffset,
            &sourceChunkSize,
            nullptr);
        if (LZ4F_isError(nextHint)) {
            return fail(output, error, LZ4F_getErrorName(nextHint));
        }
        if (!sourceChunkSize && !destinationChunkSize) {
            return fail(output, error, "LZ4 decompressor made no progress");
        }
        sourceOffset += sourceChunkSize;
        destinationOffset += destinationChunkSize;
        if (destinationOffset > output->size()) {
            return fail(output, error, "decompressed size exceeds frame declaration");
        }
    }

    if (sourceOffset != frameSize) {
        return fail(output, error, "trailing data after LZ4 frame");
    }
    if (destinationOffset != output->size()) {
        return fail(output, error, "decompressed size does not match frame declaration");
    }
    if (encoding) {
        *encoding = Encoding::Lz4;
    }
    return true;
}
} // namespace DewpointUgc
