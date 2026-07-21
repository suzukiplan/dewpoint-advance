#include "ugc_codec.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace
{
constexpr size_t UGC_SIZE_LIMIT = 1024 * 1024;

std::vector<uint8_t> makeData(size_t size)
{
    std::vector<uint8_t> data(size);
    uint32_t state = 0x9E3779B9u;
    for (uint8_t& byte : data) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        byte = static_cast<uint8_t>(state);
    }
    return data;
}

std::vector<uint8_t> compress(const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> compressed;
    std::string error;
    assert(DewpointUgc::compress(
        data.data(), data.size(), UGC_SIZE_LIMIT, &compressed, &error));
    assert(error.empty());
    assert(!compressed.empty());
    assert(compressed.size() <= DewpointUgc::maxCompressedSize(data.size()));
    assert(compressed.size() > 16);
    assert(std::equal(compressed.begin(), compressed.begin() + 11, "DPA-UGC-LZ4"));
    return compressed;
}

void assertRoundTrip(const std::vector<uint8_t>& data)
{
    const std::vector<uint8_t> compressed = compress(data);
    std::vector<uint8_t> decompressed;
    DewpointUgc::Encoding encoding = DewpointUgc::Encoding::LegacyUncompressed;
    std::string error;
    assert(DewpointUgc::extract(
        compressed.data(), compressed.size(), UGC_SIZE_LIMIT, &decompressed, &encoding, &error));
    assert(error.empty());
    assert(encoding == DewpointUgc::Encoding::Lz4);
    assert(decompressed == data);
}

void testRoundTrips()
{
    assertRoundTrip(std::vector<uint8_t>{0x78, 0x56, 0x34, 0x12});
    assertRoundTrip(std::vector<uint8_t>(256 * 1024, 0xA5));
    assertRoundTrip(makeData(UGC_SIZE_LIMIT));
}

void testCompressionLimit()
{
    const std::vector<uint8_t> data(8, 0x11);
    std::vector<uint8_t> output{0xFF};
    std::string error;
    assert(!DewpointUgc::compress(data.data(), data.size(), data.size() - 1, &output, &error));
    assert(output.empty());
    assert(!error.empty());

    assert(!DewpointUgc::compress(nullptr, 0, UGC_SIZE_LIMIT, &output, &error));
    assert(output.empty());
    assert(!error.empty());
}

void testLegacyExtraction()
{
    const std::vector<uint8_t> legacy = makeData(128 * 1024);
    std::vector<uint8_t> output;
    DewpointUgc::Encoding encoding = DewpointUgc::Encoding::Lz4;
    std::string error;
    assert(DewpointUgc::extract(
        legacy.data(), legacy.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(error.empty());
    assert(encoding == DewpointUgc::Encoding::LegacyUncompressed);
    assert(output == legacy);

    const std::vector<uint8_t> unaligned{1, 2, 3};
    assert(!DewpointUgc::extract(
        unaligned.data(), unaligned.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());

    const std::vector<uint8_t> oversized(UGC_SIZE_LIMIT + sizeof(uint32_t), 0x55);
    assert(!DewpointUgc::extract(
        oversized.data(), oversized.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());
}

void testRejectsInvalidFrames()
{
    const std::vector<uint8_t> original = makeData(128 * 1024);
    const std::vector<uint8_t> valid = compress(original);
    std::vector<uint8_t> output{0xFF};
    DewpointUgc::Encoding encoding = DewpointUgc::Encoding::LegacyUncompressed;
    std::string error;

    std::vector<uint8_t> corrupted = valid;
    corrupted[corrupted.size() / 2] ^= 0x80;
    assert(!DewpointUgc::extract(
        corrupted.data(), corrupted.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());

    std::vector<uint8_t> truncated = valid;
    truncated.pop_back();
    assert(!DewpointUgc::extract(
        truncated.data(), truncated.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());

    std::vector<uint8_t> withTrailingData = valid;
    withTrailingData.push_back(0);
    assert(!DewpointUgc::extract(
        withTrailingData.data(), withTrailingData.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());

    std::vector<uint8_t> unsupportedHeader = valid;
    unsupportedHeader[12] = 0xFF;
    assert(!DewpointUgc::extract(
        unsupportedHeader.data(), unsupportedHeader.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());

    std::vector<uint8_t> truncatedHeader(valid.begin(), valid.begin() + 12);
    assert(!DewpointUgc::extract(
        truncatedHeader.data(), truncatedHeader.size(), UGC_SIZE_LIMIT, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());
}

void testDecompressionLimit()
{
    const std::vector<uint8_t> original(64, 0x44);
    const std::vector<uint8_t> compressed = compress(original);
    std::vector<uint8_t> output;
    DewpointUgc::Encoding encoding = DewpointUgc::Encoding::LegacyUncompressed;
    std::string error;
    assert(!DewpointUgc::extract(
        compressed.data(), compressed.size(), original.size() - 1, &output, &encoding, &error));
    assert(output.empty());
    assert(!error.empty());
}
} // namespace

int main()
{
    testRoundTrips();
    testCompressionLimit();
    testLegacyExtraction();
    testRejectsInvalidFrames();
    testDecompressionLimit();
    std::cout << "ugc_codec_test: ok\n";
    return 0;
}
