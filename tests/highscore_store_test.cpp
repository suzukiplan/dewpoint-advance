#include "highscore_store.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace
{
uint32_t readU32(const std::vector<uint8_t>& data, size_t offset)
{
    return static_cast<uint32_t>(data[offset]) |
           (static_cast<uint32_t>(data[offset + 1]) << 8) |
           (static_cast<uint32_t>(data[offset + 2]) << 16) |
           (static_cast<uint32_t>(data[offset + 3]) << 24);
}

std::vector<uint8_t> readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::vector<uint8_t>(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}
} // namespace

int main()
{
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("dewpoint-highscore-store-test-" + std::to_string(DewpointHighScore::Store::createRequestId()));
    std::filesystem::create_directory(directory);

    std::vector<std::string> logs;
    DewpointHighScore::Store store([&logs](const char* message) {
        logs.emplace_back(message ? message : "");
    });
    assert(store.setDirectory(directory.string()));
    assert(store.isConfigured());
    assert(store.getUgcSizeLimit() == DewpointUgc::DEFAULT_SIZE_LIMIT);
    assert(!store.setUgcSizeLimit(0));
    assert(!store.setUgcSizeLimit(5));
    assert(!store.setUgcSizeLimit(DewpointUgc::MAX_SIZE_LIMIT + 1u));
    assert(store.getUgcSizeLimit() == DewpointUgc::DEFAULT_SIZE_LIMIT);

    DewpointHighScore::Record loaded;
    assert(store.load(0, &loaded) == DewpointHighScore::LoadResult::Missing);

    DewpointHighScore::Record record;
    record.score = -123456;
    record.ugc = {0x78, 0x56, 0x34, 0x12, 0xEF, 0xCD, 0xAB, 0x90};
    record.requestId = DewpointHighScore::Store::createRequestId();
    record.steamId = 76561198000000000ull;
    assert(store.savePending(0, record));

    const std::filesystem::path boardPath = directory / "board0.dat";
#ifndef _WIN32
    struct stat directoryStatus{};
    struct stat fileStatus{};
    assert(stat(directory.c_str(), &directoryStatus) == 0);
    assert(stat(boardPath.c_str(), &fileStatus) == 0);
    assert((directoryStatus.st_mode & 0777) == 0700);
    assert((fileStatus.st_mode & 0777) == 0600);
#endif
    const std::vector<uint8_t> bytes = readFile(boardPath);
    assert(bytes.size() == 12 + record.ugc.size() + 28);
    assert(readU32(bytes, 0) == 0x00000000u);
    assert(static_cast<int32_t>(readU32(bytes, 4)) == record.score);
    assert(readU32(bytes, 8) == record.ugc.size());
    assert(std::equal(record.ugc.begin(), record.ugc.end(), bytes.begin() + 12));

    assert(store.load(0, &loaded) == DewpointHighScore::LoadResult::Pending);
    assert(loaded.score == record.score);
    assert(loaded.ugc == record.ugc);
    assert(loaded.requestId == record.requestId);
    assert(loaded.steamId == record.steamId);

    assert(store.setUgcSizeLimit(static_cast<uint32_t>(record.ugc.size())));
    DewpointHighScore::Record oversized = record;
    oversized.ugc.insert(oversized.ugc.end(), sizeof(uint32_t), 0);
    assert(!store.savePending(2, oversized));
    assert(store.load(0, &loaded) == DewpointHighScore::LoadResult::Pending);
    assert(store.setUgcSizeLimit(sizeof(uint32_t)));
    assert(store.load(0, &loaded) == DewpointHighScore::LoadResult::LimitExceeded);
    assert(std::filesystem::exists(boardPath));
    assert(store.setUgcSizeLimit(DewpointUgc::DEFAULT_SIZE_LIMIT));

    assert(store.markProcessed(0, record));
    assert(store.load(0, &loaded) == DewpointHighScore::LoadResult::Processed);
    assert(readU32(readFile(boardPath), 0) == 0xFFFFFFFFu);

    assert(store.savePending(1, record));
    const std::filesystem::path corruptPath = directory / "board1.dat";
    std::vector<uint8_t> corruptBytes = readFile(corruptPath);
    corruptBytes[12] ^= 0xFF;
    {
        std::ofstream output(corruptPath, std::ios::binary | std::ios::trunc);
        output.write(reinterpret_cast<const char*>(corruptBytes.data()), corruptBytes.size());
    }
    assert(store.load(1, &loaded) == DewpointHighScore::LoadResult::Invalid);
    assert(store.quarantine(1));
    assert(!std::filesystem::exists(corruptPath));
    assert(std::filesystem::exists(directory / "board1.dat.invalid"));

    DewpointHighScore::Record unaligned = record;
    unaligned.ugc.push_back(0);
    assert(!store.savePending(2, unaligned));
    assert(!store.savePending(-1, record));
    assert(!store.savePending(DewpointHighScore::BOARD_COUNT, record));
    assert(DewpointHighScore::Store::createRequestId() != DewpointHighScore::Store::createRequestId());

    std::filesystem::remove_all(directory);
    return 0;
}
