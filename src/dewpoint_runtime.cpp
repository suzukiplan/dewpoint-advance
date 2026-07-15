/**
 * Dewpoint Advance Runtime
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#include "dewpoint_runtime.h"

#include "CSteamLeaderboardHelper.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <steam_api.h>

namespace
{
constexpr uint32_t DPMID = ('D' << 24) | ('E' << 16) | ('W' << 8) | 'P';
constexpr int BOARD_COUNT = 16;
constexpr size_t MAX_ACHIEVEMENT_ID = 127;
constexpr size_t MAX_UGC_SIZE = 1024 * 1024;

enum DpaIndex : uint32_t {
    DpaIndexId = 0,
    DpaIndexAchievement,
    DpaIndexSetBoardId,
    DpaIndexSetUgcOption,
    DpaIndexSendScore,
    DpaIndexBoardReady,
    DpaIndexBoardEntry,
    DpaIndexBoardEntryGet,
    DpaIndexUgcClear,
    DpaIndexUgcAppend,
    DpaIndexUgcDownload,
    DpaIndexUgcSize,
    DpaIndexUgcReadPtr,
    DpaIndexUgcRead,
    DpaIndexExit,
    DpaIndexFullScreen,
};

struct GuestLeaderboardEntry {
    int32_t boardId;
    int32_t rank;
    int32_t score;
    int32_t isMyRank;
    char name[16];
};
static_assert(sizeof(GuestLeaderboardEntry) == 32, "Unexpected GBA leaderboard entry layout");

void copyAsciiName(char (&destination)[16], const char* source)
{
    std::memset(destination, 0, sizeof(destination));
    if (!source) {
        return;
    }

    size_t output = 0;
    const auto* input = reinterpret_cast<const uint8_t*>(source);
    while (*input && output + 1 < sizeof(destination)) {
        if (*input < 0x80) {
            destination[output++] = static_cast<char>(*input++);
            continue;
        }
        destination[output++] = '?';
        ++input;
        while ((*input & 0xC0) == 0x80) {
            ++input;
        }
    }
}
} // namespace

struct DewpointRuntime::Impl {
    struct EntryReference {
        int boardId;
        int index;
    };

    mGBAHelper& gba;
    Logger logger;
    FullscreenSetter fullscreenSetter;
    FullscreenGetter fullscreenGetter;
    bool steamInitialized;
    bool fullscreen;
    bool exitRequested;
    int exitCode;
    int selectedBoardId;
    bool sendUgc;
    uint32_t guestEntryAddress;
    uint32_t ugcReadIndex;
    int32_t ugcDownloadSize;
    uint64_t protocolGeneration;
    std::string achievementId;
    bool achievementOverflow;
    std::vector<uint8_t> ugcUploadData;
    bool ugcUploadOverflow;
    std::vector<uint8_t> ugcDownloadData;
    std::array<std::unique_ptr<CSteamLeaderboardHelper>, BOARD_COUNT> boards;
    std::array<bool, BOARD_COUNT> boardInitializationAttempted;
    std::unordered_map<uint32_t, EntryReference> entryReferences;

    Impl(mGBAHelper& gba, Logger logger)
        : gba(gba), logger(std::move(logger)), steamInitialized(false), fullscreen(false), exitRequested(false),
          exitCode(0), selectedBoardId(-1), sendUgc(false), guestEntryAddress(0), ugcReadIndex(0),
          ugcDownloadSize(0), protocolGeneration(0),
          achievementOverflow(false), ugcUploadOverflow(false), boardInitializationAttempted{}
    {
    }

    ~Impl()
    {
        boards = {};
        if (steamInitialized) {
            SteamAPI_Shutdown();
        }
    }

    void log(const std::string& message) const
    {
        if (logger) {
            logger(message.c_str());
        }
    }

    bool validBoardId(int boardId) const
    {
        return boardId >= 0 && boardId < BOARD_COUNT;
    }

    CSteamLeaderboardHelper* getBoard(int boardId, bool initialize)
    {
        if (!steamInitialized || !validBoardId(boardId)) {
            return nullptr;
        }
        auto& board = boards[static_cast<size_t>(boardId)];
        if (!board) {
            board = std::make_unique<CSteamLeaderboardHelper>(
                "board" + std::to_string(boardId), "ugc", MAX_UGC_SIZE, logger);
            board->setMaxEntries(100);
        }
        if (initialize && !boardInitializationAttempted[static_cast<size_t>(boardId)]) {
            boardInitializationAttempted[static_cast<size_t>(boardId)] = true;
            board->initialize();
        }
        return board.get();
    }

    LeaderboardEntry_t* resolveEntry(const EntryReference& reference)
    {
        CSteamLeaderboardHelper* board = getBoard(reference.boardId, false);
        if (!board) {
            return nullptr;
        }
        return reference.index < 0 ? board->getMyEntry() : board->getEntry(reference.index);
    }

    bool writeEntry(int index)
    {
        CSteamLeaderboardHelper* board = getBoard(selectedBoardId, true);
        if (!board || !board->isReady() || !guestEntryAddress || index < -1 || index >= 100) {
            return false;
        }
        LeaderboardEntry_t* source = index < 0 ? board->getMyEntry() : board->getEntry(index);
        if (!source) {
            return false;
        }

        GuestLeaderboardEntry destination{};
        destination.boardId = selectedBoardId;
        destination.rank = source->m_nGlobalRank;
        destination.score = source->m_nScore;
        ISteamUser* user = SteamUser();
        destination.isMyRank = user && user->GetSteamID() == source->m_steamIDUser ? 1 : 0;
        copyAsciiName(destination.name, board->getUserName(source));
        if (!gba.writeGuestMemory(guestEntryAddress, &destination, sizeof(destination))) {
            log("Rejected leaderboard destination outside GBA work RAM.");
            return false;
        }
        entryReferences[guestEntryAddress] = {selectedBoardId, index};
        return true;
    }

    void downloadUgc()
    {
        ugcDownloadData.clear();
        ugcDownloadSize = 0;
        auto reference = entryReferences.find(guestEntryAddress);
        if (reference == entryReferences.end() || reference->second.boardId != selectedBoardId) {
            ugcDownloadSize = -1;
            log("Rejected UGC download for an unknown leaderboard entry.");
            return;
        }
        CSteamLeaderboardHelper* board = getBoard(selectedBoardId, false);
        LeaderboardEntry_t* entry = resolveEntry(reference->second);
        if (!board || !entry) {
            ugcDownloadSize = -1;
            return;
        }
        const uint64_t generation = protocolGeneration;
        board->downloadUGC(entry, [this, generation](const uint8_t* data, size_t size) {
            if (generation != protocolGeneration) {
                return;
            }
            if (!data || !size || size > MAX_UGC_SIZE) {
                ugcDownloadData.clear();
                ugcDownloadSize = -1;
                return;
            }
            ugcDownloadData.assign(data, data + size);
            ugcDownloadSize = static_cast<int32_t>(size);
        });
    }

    uint32_t readUgcWord() const
    {
        const uint64_t offset = static_cast<uint64_t>(ugcReadIndex) * sizeof(uint32_t);
        if (offset + sizeof(uint32_t) > ugcDownloadData.size()) {
            return UINT32_MAX;
        }
        const size_t position = static_cast<size_t>(offset);
        return static_cast<uint32_t>(ugcDownloadData[position]) |
               (static_cast<uint32_t>(ugcDownloadData[position + 1]) << 8) |
               (static_cast<uint32_t>(ugcDownloadData[position + 2]) << 16) |
               (static_cast<uint32_t>(ugcDownloadData[position + 3]) << 24);
    }

    void appendUgcWord(uint32_t value)
    {
        if (ugcUploadData.size() > MAX_UGC_SIZE - sizeof(value)) {
            if (!ugcUploadOverflow) {
                log("UGC upload buffer limit exceeded; upload rejected.");
                ugcUploadOverflow = true;
            }
            return;
        }
        ugcUploadData.push_back(static_cast<uint8_t>(value));
        ugcUploadData.push_back(static_cast<uint8_t>(value >> 8));
        ugcUploadData.push_back(static_cast<uint8_t>(value >> 16));
        ugcUploadData.push_back(static_cast<uint8_t>(value >> 24));
    }

    void submitAchievement()
    {
        if (!achievementOverflow && !achievementId.empty()) {
            ISteamUserStats* stats = SteamUserStats();
            if (stats && stats->SetAchievement(achievementId.c_str())) {
                if (!stats->StoreStats()) {
                    log("Failed to persist Steam achievement: " + achievementId);
                }
            } else {
                log("Failed to unlock Steam achievement: " + achievementId);
            }
        }
        achievementId.clear();
        achievementOverflow = false;
    }

    void resetProtocol()
    {
        ++protocolGeneration;
        selectedBoardId = -1;
        sendUgc = false;
        guestEntryAddress = 0;
        ugcReadIndex = 0;
        ugcDownloadSize = 0;
        achievementId.clear();
        achievementOverflow = false;
        ugcUploadData.clear();
        ugcUploadOverflow = false;
        ugcDownloadData.clear();
        entryReferences.clear();
    }
};

DewpointRuntime::DewpointRuntime(mGBAHelper& gba, Logger logger)
    : impl(std::make_unique<Impl>(gba, std::move(logger)))
{
    gba.setDewpointBridge(this);
}

DewpointRuntime::~DewpointRuntime()
{
    impl->gba.setDewpointBridge(nullptr);
}

bool DewpointRuntime::initialize()
{
    if (impl->steamInitialized) {
        return true;
    }
    if (!SteamAPI_Init()) {
        impl->log("SteamAPI_Init failed; Dewpoint SDK bridge is disabled.");
        return false;
    }
    impl->steamInitialized = true;
    return true;
}

void DewpointRuntime::tick()
{
    if (impl->steamInitialized) {
        SteamAPI_RunCallbacks();
    }
}

void DewpointRuntime::setFullscreenCallbacks(FullscreenSetter setter, FullscreenGetter getter)
{
    impl->fullscreenSetter = std::move(setter);
    impl->fullscreenGetter = std::move(getter);
    if (impl->fullscreenGetter) {
        impl->fullscreen = impl->fullscreenGetter();
    }
}

bool DewpointRuntime::takeExitRequest(int* exitCode)
{
    if (!impl->exitRequested) {
        return false;
    }
    if (exitCode) {
        *exitCode = impl->exitCode;
    }
    impl->exitRequested = false;
    return true;
}

uint32_t DewpointRuntime::readRegister(uint32_t index)
{
    switch (index) {
        case DpaIndexId: return DPMID;
        case DpaIndexFullScreen:
            if (impl->fullscreenGetter) {
                impl->fullscreen = impl->fullscreenGetter();
            }
            return impl->fullscreen ? 1 : 0;
        case DpaIndexBoardReady: {
            CSteamLeaderboardHelper* board = impl->getBoard(impl->selectedBoardId, true);
            return board && board->isReady() ? 1 : 0;
        }
        case DpaIndexUgcSize: return static_cast<uint32_t>(impl->ugcDownloadSize);
        case DpaIndexUgcRead: return impl->readUgcWord();
        default: return 0;
    }
}

void DewpointRuntime::writeRegister(uint32_t index, uint32_t value)
{
    if (index == DpaIndexExit) {
        if (!impl->exitRequested) {
            impl->exitRequested = true;
            impl->exitCode = static_cast<int32_t>(value);
        }
        return;
    }
    if (index == DpaIndexFullScreen) {
        const bool requested = value != 0;
        impl->fullscreen = impl->fullscreenSetter ? impl->fullscreenSetter(requested) : requested;
        return;
    }
    if (!impl->steamInitialized) {
        return;
    }
    switch (index) {
        case DpaIndexAchievement:
            if (!value) {
                impl->submitAchievement();
            } else if (!impl->achievementOverflow) {
                if (impl->achievementId.size() >= MAX_ACHIEVEMENT_ID) {
                    impl->achievementOverflow = true;
                } else {
                    impl->achievementId.push_back(static_cast<char>(value & 0xFF));
                }
            }
            break;
        case DpaIndexSetBoardId:
            impl->selectedBoardId = impl->validBoardId(static_cast<int32_t>(value)) ? static_cast<int32_t>(value) : -1;
            if (impl->selectedBoardId >= 0) {
                impl->getBoard(impl->selectedBoardId, true);
            }
            break;
        case DpaIndexSetUgcOption: impl->sendUgc = value != 0; break;
        case DpaIndexSendScore: {
            CSteamLeaderboardHelper* board = impl->getBoard(impl->selectedBoardId, true);
            if (board) {
                if (impl->sendUgc && impl->ugcUploadOverflow) {
                    impl->log("Score upload rejected because its UGC buffer overflowed.");
                } else if (impl->sendUgc && !impl->ugcUploadData.empty()) {
                    board->sendScore(static_cast<int32_t>(value), impl->ugcUploadData.data(), impl->ugcUploadData.size());
                } else {
                    board->sendScore(static_cast<int32_t>(value));
                }
            }
            break;
        }
        case DpaIndexBoardEntry: impl->guestEntryAddress = value; break;
        case DpaIndexBoardEntryGet: impl->writeEntry(static_cast<int32_t>(value)); break;
        case DpaIndexUgcClear:
            impl->ugcUploadData.clear();
            impl->ugcUploadOverflow = false;
            break;
        case DpaIndexUgcAppend: impl->appendUgcWord(value); break;
        case DpaIndexUgcDownload: impl->downloadUgc(); break;
        case DpaIndexUgcReadPtr: impl->ugcReadIndex = value; break;
        default: break;
    }
}

void DewpointRuntime::reset()
{
    impl->resetProtocol();
}
