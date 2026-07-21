/**
 * Dewpoint Advance Pending High Score Store
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#pragma once

#include "ugc_limits.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace DewpointHighScore
{
constexpr int BOARD_COUNT = 16;

struct Record {
    int32_t score = 0;
    std::vector<uint8_t> ugc;
    uint64_t requestId = 0;
    uint64_t steamId = 0;
};

enum class LoadResult {
    Missing,
    Pending,
    Processed,
    LimitExceeded,
    Invalid,
    Error,
};

class Store final
{
  public:
    using Logger = std::function<void(const char*)>;

    explicit Store(Logger logger = {});

    bool setUgcSizeLimit(uint32_t size);
    uint32_t getUgcSizeLimit() const;

    bool setDirectory(std::string directory);
    bool isConfigured() const;
    const std::string& getDirectory() const;

    LoadResult load(int boardId, Record* record);
    bool savePending(int boardId, const Record& record);
    bool markProcessed(int boardId, const Record& record);
    bool quarantine(int boardId);

    static uint64_t createRequestId();

  private:
    bool validBoardId(int boardId) const;
    std::string pathForBoard(int boardId) const;
    bool save(int boardId, const Record& record, bool processed);
    void log(const std::string& message) const;

    Logger logger;
    std::string directory;
    uint32_t ugcSizeLimit;
};
} // namespace DewpointHighScore
