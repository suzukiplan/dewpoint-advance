/**
 * Dewpoint Advance Pending High Score Store
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#include "highscore_store.h"

#include "pathutil.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <utility>

#ifdef _WIN32
#include <Windows.h>
#include <io.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace DewpointHighScore
{
namespace
{
constexpr uint32_t PENDING_FLAG = 0x00000000u;
constexpr uint32_t PROCESSED_FLAG = 0xFFFFFFFFu;
constexpr std::array<uint8_t, 4> FOOTER_MAGIC{{'D', 'P', 'H', 'S'}};
constexpr uint32_t FORMAT_VERSION = 1;
constexpr size_t HEADER_SIZE = 12;
constexpr size_t FOOTER_SIZE = 28;

// The first 12 bytes and UGC offset retain the public format:
// flag, signed score, UGC size, then UGC. The footer adds validation and ownership.

uint32_t readU32(const uint8_t* data)
{
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t readU64(const uint8_t* data)
{
    return static_cast<uint64_t>(readU32(data)) |
           (static_cast<uint64_t>(readU32(data + 4)) << 32);
}

void appendU32(std::vector<uint8_t>* data, uint32_t value)
{
    data->push_back(static_cast<uint8_t>(value));
    data->push_back(static_cast<uint8_t>(value >> 8));
    data->push_back(static_cast<uint8_t>(value >> 16));
    data->push_back(static_cast<uint8_t>(value >> 24));
}

void appendU64(std::vector<uint8_t>* data, uint64_t value)
{
    appendU32(data, static_cast<uint32_t>(value));
    appendU32(data, static_cast<uint32_t>(value >> 32));
}

uint32_t crc32(const uint8_t* data, size_t size)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

uint32_t recordCrc(const std::vector<uint8_t>& bytes, size_t ugcSize, uint64_t requestId, uint64_t steamId)
{
    std::vector<uint8_t> checksumData;
    checksumData.reserve(8 + ugcSize + 20);
    checksumData.insert(checksumData.end(), bytes.begin() + 4, bytes.begin() + HEADER_SIZE + ugcSize);
    checksumData.insert(checksumData.end(), FOOTER_MAGIC.begin(), FOOTER_MAGIC.end());
    appendU32(&checksumData, FORMAT_VERSION);
    appendU64(&checksumData, requestId);
    appendU64(&checksumData, steamId);
    return crc32(checksumData.data(), checksumData.size());
}

bool writeDurably(const std::string& path, const std::vector<uint8_t>& data)
{
    FILE* file = nullptr;
#ifdef _WIN32
    if (fopen_s(&file, path.c_str(), "wb") != 0) {
        return false;
    }
#else
    int flags = O_WRONLY | O_CREAT | O_TRUNC;
#ifdef O_NOFOLLOW
    flags |= O_NOFOLLOW;
#endif
    const int descriptor = open(path.c_str(), flags, S_IRUSR | S_IWUSR);
    if (descriptor >= 0) {
        file = fdopen(descriptor, "wb");
        if (!file) {
            close(descriptor);
        }
    }
#endif
    if (!file) {
        return false;
    }

    const bool wrote = data.empty() || std::fwrite(data.data(), 1, data.size(), file) == data.size();
    bool synced = wrote && std::fflush(file) == 0;
    if (synced) {
#ifdef _WIN32
        synced = _commit(_fileno(file)) == 0;
#else
        synced = fsync(fileno(file)) == 0;
#endif
    }
    const bool closed = std::fclose(file) == 0;
    return wrote && synced && closed;
}

bool replaceFile(const std::string& temporary, const std::string& destination)
{
#ifdef _WIN32
    return MoveFileExA(
               temporary.c_str(),
               destination.c_str(),
               MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(temporary.c_str(), destination.c_str()) == 0;
#endif
}

bool syncDirectory(const std::string& directory)
{
#ifdef _WIN32
    (void)directory;
    return true;
#else
    const int descriptor = open(directory.c_str(), O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        return false;
    }
    const bool synced = fsync(descriptor) == 0;
    const bool closed = close(descriptor) == 0;
    return synced && closed;
#endif
}
} // namespace

Store::Store(Logger logger)
    : logger(std::move(logger)), ugcSizeLimit(DewpointUgc::DEFAULT_SIZE_LIMIT)
{
}

bool Store::setUgcSizeLimit(uint32_t size)
{
    if (!DewpointUgc::isValidSizeLimit(size)) {
        return false;
    }
    ugcSizeLimit = size;
    return true;
}

uint32_t Store::getUgcSizeLimit() const
{
    return ugcSizeLimit;
}

bool Store::setDirectory(std::string directory)
{
    if (directory.empty()) {
        return false;
    }
    bool exists = false;
    bool isDirectory = false;
    std::string errorMessage;
    if (!DewpointPath::inspect(directory, &exists, &isDirectory, &errorMessage) || !exists || !isDirectory) {
        log("Pending high score directory is unavailable: " + directory +
            (errorMessage.empty() ? std::string() : ": " + errorMessage));
        return false;
    }
#ifndef _WIN32
    if (chmod(directory.c_str(), S_IRWXU) != 0) {
        log("Failed to restrict pending high score directory permissions: " + directory);
        return false;
    }
#endif
    this->directory = std::move(directory);
    return true;
}

bool Store::isConfigured() const
{
    return !directory.empty();
}

const std::string& Store::getDirectory() const
{
    return directory;
}

LoadResult Store::load(int boardId, Record* record)
{
    if (!isConfigured() || !validBoardId(boardId) || !record) {
        return LoadResult::Error;
    }
    const std::string path = pathForBoard(boardId);
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        bool exists = false;
        bool isDirectory = false;
        if (DewpointPath::inspect(path, &exists, &isDirectory, nullptr) && !exists) {
            return LoadResult::Missing;
        }
        log("Failed to open pending high score file: " + path);
        return LoadResult::Error;
    }
    const std::streamsize fileSize = input.tellg();
    if (fileSize < static_cast<std::streamsize>(HEADER_SIZE + FOOTER_SIZE) ||
        static_cast<uint64_t>(fileSize) > HEADER_SIZE + static_cast<uint64_t>(DewpointUgc::MAX_SIZE_LIMIT) + FOOTER_SIZE) {
        log("Rejected pending high score file with an invalid size: " + path);
        return LoadResult::Invalid;
    }

    std::array<uint8_t, HEADER_SIZE> header{};
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(header.data()), header.size())) {
        log("Failed to read pending high score header: " + path);
        return LoadResult::Error;
    }
    const uint32_t flag = readU32(header.data());
    const uint32_t ugcSize = readU32(header.data() + 8);
    if ((flag != PENDING_FLAG && flag != PROCESSED_FLAG) ||
        ugcSize > DewpointUgc::MAX_SIZE_LIMIT ||
        (ugcSize % sizeof(uint32_t)) != 0 ||
        static_cast<uint64_t>(fileSize) != HEADER_SIZE + static_cast<uint64_t>(ugcSize) + FOOTER_SIZE) {
        log("Rejected malformed pending high score file: " + path);
        return LoadResult::Invalid;
    }
    if (ugcSize > ugcSizeLimit) {
        log("Deferred pending high score file above the current UGC size limit: " + path);
        return LoadResult::LimitExceeded;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(fileSize));
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), fileSize)) {
        log("Failed to read pending high score file: " + path);
        return LoadResult::Error;
    }

    const uint8_t* footer = bytes.data() + HEADER_SIZE + ugcSize;
    if (!std::equal(FOOTER_MAGIC.begin(), FOOTER_MAGIC.end(), footer) ||
        readU32(footer + 4) != FORMAT_VERSION) {
        log("Rejected unsupported pending high score file: " + path);
        return LoadResult::Invalid;
    }
    const uint32_t storedCrc = readU32(footer + 8);
    const uint64_t requestId = readU64(footer + 12);
    const uint64_t steamId = readU64(footer + 20);
    if (!requestId || storedCrc != recordCrc(bytes, ugcSize, requestId, steamId)) {
        log("Rejected corrupt pending high score file: " + path);
        return LoadResult::Invalid;
    }

    record->score = static_cast<int32_t>(readU32(bytes.data() + 4));
    record->ugc.assign(bytes.begin() + HEADER_SIZE, bytes.begin() + HEADER_SIZE + ugcSize);
    record->requestId = requestId;
    record->steamId = steamId;
    return flag == PENDING_FLAG ? LoadResult::Pending : LoadResult::Processed;
}

bool Store::savePending(int boardId, const Record& record)
{
    return save(boardId, record, false);
}

bool Store::markProcessed(int boardId, const Record& record)
{
    return save(boardId, record, true);
}

bool Store::quarantine(int boardId)
{
    if (!isConfigured() || !validBoardId(boardId)) {
        return false;
    }
    const std::string source = pathForBoard(boardId);
    for (unsigned int suffix = 0; suffix < 1000; ++suffix) {
        const std::string destination = source + ".invalid" + (suffix ? "." + std::to_string(suffix) : "");
        bool exists = false;
        bool isDirectory = false;
        if (!DewpointPath::inspect(destination, &exists, &isDirectory, nullptr) || exists) {
            continue;
        }
        if (std::rename(source.c_str(), destination.c_str()) == 0) {
            log("Quarantined invalid pending high score file: " + destination);
            return true;
        }
        break;
    }
    log("Failed to quarantine invalid pending high score file: " + source);
    return false;
}

uint64_t Store::createRequestId()
{
    static std::atomic<uint64_t> sequence{0};
    const uint64_t timestamp = static_cast<uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
#ifdef _WIN32
    const uint64_t processId = static_cast<uint64_t>(GetCurrentProcessId());
#else
    const uint64_t processId = static_cast<uint64_t>(getpid());
#endif
    uint64_t requestId = timestamp ^ (processId << 32) ^ (++sequence * 0x9E3779B97F4A7C15ull);
    return requestId ? requestId : 1;
}

bool Store::validBoardId(int boardId) const
{
    return boardId >= 0 && boardId < BOARD_COUNT;
}

std::string Store::pathForBoard(int boardId) const
{
    return DewpointPath::join(directory, "board" + std::to_string(boardId) + ".dat");
}

bool Store::save(int boardId, const Record& record, bool processed)
{
    if (!isConfigured() || !validBoardId(boardId) || !record.requestId ||
        record.ugc.size() > ugcSizeLimit || (record.ugc.size() % sizeof(uint32_t)) != 0) {
        return false;
    }

    std::vector<uint8_t> bytes;
    bytes.reserve(HEADER_SIZE + record.ugc.size() + FOOTER_SIZE);
    appendU32(&bytes, processed ? PROCESSED_FLAG : PENDING_FLAG);
    appendU32(&bytes, static_cast<uint32_t>(record.score));
    appendU32(&bytes, static_cast<uint32_t>(record.ugc.size()));
    bytes.insert(bytes.end(), record.ugc.begin(), record.ugc.end());
    bytes.insert(bytes.end(), FOOTER_MAGIC.begin(), FOOTER_MAGIC.end());
    appendU32(&bytes, FORMAT_VERSION);
    const size_t crcPosition = bytes.size();
    appendU32(&bytes, 0);
    appendU64(&bytes, record.requestId);
    appendU64(&bytes, record.steamId);
    const uint32_t checksum = recordCrc(bytes, record.ugc.size(), record.requestId, record.steamId);
    bytes[crcPosition] = static_cast<uint8_t>(checksum);
    bytes[crcPosition + 1] = static_cast<uint8_t>(checksum >> 8);
    bytes[crcPosition + 2] = static_cast<uint8_t>(checksum >> 16);
    bytes[crcPosition + 3] = static_cast<uint8_t>(checksum >> 24);

    const std::string destination = pathForBoard(boardId);
    const std::string temporary = destination + "." + std::to_string(record.requestId) + ".tmp";
    if (!writeDurably(temporary, bytes)) {
        log("Failed to write pending high score file: " + temporary);
        return false;
    }
    if (!replaceFile(temporary, destination)) {
        log("Failed to replace pending high score file: " + destination);
        std::remove(temporary.c_str());
        return false;
    }
    if (!syncDirectory(directory)) {
        log("Failed to synchronize pending high score directory: " + directory);
        return false;
    }
    return true;
}

void Store::log(const std::string& message) const
{
    if (logger) {
        logger(message.c_str());
    }
}
} // namespace DewpointHighScore
