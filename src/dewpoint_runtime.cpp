/**
 * Dewpoint Advance Runtime
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#include "dewpoint_runtime.h"

#include "CSteamLeaderboardHelper.hpp"
#include "dewpoint_define.h"
#include "highscore_store.h"
#include "ugc_limits.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <steam_api.h>

namespace
{
constexpr uint32_t DPMID = ('D' << 24) | ('E' << 16) | ('W' << 8) | 'P';
constexpr int BOARD_COUNT = DewpointHighScore::BOARD_COUNT;
constexpr size_t MAX_ACHIEVEMENT_ID = 127;

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
    DpaAppVersion,
    DpaButtonA,
    DpaButtonB,
    DpaIndexUgcLimitSize,
};
static_assert(DpaIndexUgcLimitSize + 1 == DewpointBridge::REGISTER_COUNT, "Dewpoint register count is out of sync");

struct ButtonCharacters {
    char a;
    char b;
};

constexpr ButtonCharacters getButtonCharacters(DewpointRuntime::ButtonInputType inputType)
{
    switch (inputType) {
        case DewpointRuntime::ButtonInputType::XboxOrSwitch: return {'A', 'B'};
        case DewpointRuntime::ButtonInputType::PlayStation: return {'X', 'O'};
        case DewpointRuntime::ButtonInputType::PCKeyboard: return {'X', 'Z'};
    }
    return {'X', 'Z'};
}

static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::PCKeyboard).a == 'X');
static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::PCKeyboard).b == 'Z');
static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::XboxOrSwitch).a == 'A');
static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::XboxOrSwitch).b == 'B');
static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::PlayStation).a == 'X');
static_assert(getButtonCharacters(DewpointRuntime::ButtonInputType::PlayStation).b == 'O');

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

    struct UploadSlot {
        std::optional<DewpointHighScore::Record> record;
        uint64_t attemptedRequestId = 0;
        uint64_t inFlightRequestId = 0;
    };

    mGBAHelper& gba;
    Logger logger;
    DewpointHighScore::Store highScoreStore;
    FullscreenSetter fullscreenSetter;
    FullscreenGetter fullscreenGetter;
    bool steamInitialized;
    ButtonInputType buttonInputType;
    bool fullscreen;
    bool exitRequested;
    int exitCode;
    int selectedBoardId;
    bool sendUgc;
    uint32_t guestEntryAddress;
    uint32_t ugcReadIndex;
    uint32_t ugcSizeLimit;
    int32_t ugcSize;
    uint64_t ugcGeneration;
    std::string achievementId;
    bool achievementOverflow;
    std::vector<uint8_t> ugcData;
    bool ugcOverflow;
    std::array<std::unique_ptr<CSteamLeaderboardHelper>, BOARD_COUNT> boards;
    std::array<bool, BOARD_COUNT> boardInitializationAttempted;
    std::array<UploadSlot, BOARD_COUNT> uploadSlots;
    std::array<bool, BOARD_COUNT> pendingLoadDeferred;
    std::unordered_map<uint32_t, EntryReference> entryReferences;

    Impl(mGBAHelper& gba, Logger logger)
        : gba(gba), logger(std::move(logger)), highScoreStore(this->logger),
          steamInitialized(false), buttonInputType(ButtonInputType::PCKeyboard),
          fullscreen(false), exitRequested(false), exitCode(0),
          selectedBoardId(-1), sendUgc(false), guestEntryAddress(0), ugcReadIndex(0),
          ugcSizeLimit(DewpointUgc::DEFAULT_SIZE_LIMIT), ugcSize(0), ugcGeneration(0),
          achievementOverflow(false), ugcOverflow(false), boardInitializationAttempted{},
          pendingLoadDeferred{}
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

    uint64_t currentSteamId() const
    {
        if (!steamInitialized) {
            return 0;
        }
        ISteamUser* user = SteamUser();
        if (!user) {
            return 0;
        }
        const CSteamID steamId = user->GetSteamID();
        return steamId.IsValid() ? steamId.ConvertToUint64() : 0;
    }

    void loadPendingRecord(int boardId)
    {
        DewpointHighScore::Record record;
        const auto result = highScoreStore.load(boardId, &record);
        const size_t index = static_cast<size_t>(boardId);
        auto& slot = uploadSlots[index];
        slot = {};
        pendingLoadDeferred[index] = result == DewpointHighScore::LoadResult::LimitExceeded;
        if (result == DewpointHighScore::LoadResult::Pending) {
            slot.record = std::move(record);
            log("Loaded pending high score for board" + std::to_string(boardId) + ".");
        } else if (result == DewpointHighScore::LoadResult::Invalid) {
            highScoreStore.quarantine(boardId);
        }
    }

    bool configureHighScoreStore(const std::string& directory)
    {
        if (!highScoreStore.setDirectory(directory)) {
            return false;
        }
        for (int boardId = 0; boardId < BOARD_COUNT; ++boardId) {
            loadPendingRecord(boardId);
        }
        return true;
    }

    CSteamLeaderboardHelper* getBoard(int boardId, bool initialize)
    {
        if (!steamInitialized || !validBoardId(boardId)) {
            return nullptr;
        }
        auto& board = boards[static_cast<size_t>(boardId)];
        if (!board) {
            board = std::make_unique<CSteamLeaderboardHelper>(
                "board" + std::to_string(boardId), "ugc", ugcSizeLimit, logger);
            board->setMaxEntries(100);
        }
        if (initialize && !boardInitializationAttempted[static_cast<size_t>(boardId)]) {
            boardInitializationAttempted[static_cast<size_t>(boardId)] = true;
            board->initialize();
        }
        return board.get();
    }

    void finishPendingRecord(int boardId, uint64_t requestId, const char* reason)
    {
        auto& slot = uploadSlots[static_cast<size_t>(boardId)];
        if (!slot.record || slot.record->requestId != requestId) {
            return;
        }
        if (!highScoreStore.markProcessed(boardId, *slot.record)) {
            log("Failed to mark high score as processed for board" + std::to_string(boardId) + ".");
            return;
        }
        log("Processed pending high score for board" + std::to_string(boardId) + " (" + reason + ").");
        slot.record.reset();
    }

    void onPendingUploadFinished(
        int boardId,
        uint64_t requestId,
        CSteamLeaderboardHelper::SendScoreResult result)
    {
        auto& slot = uploadSlots[static_cast<size_t>(boardId)];
        if (slot.inFlightRequestId == requestId) {
            slot.inFlightRequestId = 0;
        }
        if (result == CSteamLeaderboardHelper::SendScoreResult::Success) {
            finishPendingRecord(boardId, requestId, "uploaded");
        } else if (result == CSteamLeaderboardHelper::SendScoreResult::Unchanged &&
                   slot.record && slot.record->requestId == requestId && slot.record->ugc.empty()) {
            finishPendingRecord(boardId, requestId, "already stored by Steam");
        }
    }

    void processPendingBoard(int boardId)
    {
        auto& slot = uploadSlots[static_cast<size_t>(boardId)];
        if (!steamInitialized || !slot.record || slot.inFlightRequestId ||
            slot.attemptedRequestId == slot.record->requestId) {
            return;
        }

        CSteamLeaderboardHelper* board = getBoard(boardId, true);
        if (!board) {
            slot.attemptedRequestId = slot.record->requestId;
            return;
        }
        if (board->hasError()) {
            slot.attemptedRequestId = slot.record->requestId;
            log("Pending high score retry could not initialize board" + std::to_string(boardId) + ".");
            return;
        }
        if (!board->isDone() || board->isSendScoreBusy()) {
            return;
        }

        const uint64_t steamId = currentSteamId();
        if (!steamId) {
            slot.attemptedRequestId = slot.record->requestId;
            log("Pending high score retry has no authenticated Steam user for board" + std::to_string(boardId) + ".");
            return;
        }
        if (slot.record->steamId && slot.record->steamId != steamId) {
            slot.attemptedRequestId = slot.record->requestId;
            log("Pending high score belongs to another Steam account for board" + std::to_string(boardId) + ".");
            return;
        }
        if (!slot.record->steamId) {
            slot.record->steamId = steamId;
            if (!highScoreStore.savePending(boardId, *slot.record)) {
                slot.attemptedRequestId = slot.record->requestId;
                log("Failed to bind pending high score to the Steam account for board" + std::to_string(boardId) + ".");
                return;
            }
        }

        bool attachUGCWhenUnchanged = false;
        if (LeaderboardEntry_t* current = board->getMyEntry()) {
            if (current->m_nScore == slot.record->score) {
                attachUGCWhenUnchanged = !slot.record->ugc.empty();
            } else {
                bool sortOrderKnown = false;
                const bool pendingIsBetter = board->isScoreBetter(slot.record->score, current->m_nScore, &sortOrderKnown);
                if (sortOrderKnown && !pendingIsBetter) {
                    finishPendingRecord(boardId, slot.record->requestId, "superseded by a better Steam score");
                    return;
                }
            }
        }

        const uint64_t requestId = slot.record->requestId;
        const uint8_t* ugc = slot.record->ugc.empty() ? nullptr : slot.record->ugc.data();
        const size_t ugcSize = slot.record->ugc.size();
        slot.attemptedRequestId = requestId;
        slot.inFlightRequestId = requestId;
        const bool accepted = board->sendScore(
            slot.record->score,
            ugc,
            ugcSize,
            attachUGCWhenUnchanged,
            [this, boardId, requestId](CSteamLeaderboardHelper::SendScoreResult result) {
                onPendingUploadFinished(boardId, requestId, result);
            });
        if (!accepted && slot.inFlightRequestId == requestId) {
            slot.inFlightRequestId = 0;
        }
    }

    void processPendingScores()
    {
        for (int boardId = 0; boardId < BOARD_COUNT; ++boardId) {
            processPendingBoard(boardId);
        }
    }

    void submitScore(int boardId, int32_t score)
    {
        if (!validBoardId(boardId)) {
            log("Rejected high score for an invalid leaderboard.");
            return;
        }
        if (sendUgc && ugcOverflow) {
            log("Score upload rejected because its UGC buffer overflowed.");
            return;
        }

        DewpointHighScore::Record record;
        record.score = score;
        if (sendUgc && !ugcData.empty()) {
            record.ugc = ugcData;
        }
        record.requestId = DewpointHighScore::Store::createRequestId();
        record.steamId = currentSteamId();

        if (highScoreStore.isConfigured()) {
            const auto& existing = uploadSlots[static_cast<size_t>(boardId)].record;
            if (existing && existing->steamId && existing->steamId != record.steamId) {
                log("High score was not saved because an entry for another or unknown Steam account is pending on board" +
                    std::to_string(boardId) + ".");
                return;
            }
            if (highScoreStore.savePending(boardId, record)) {
                auto& slot = uploadSlots[static_cast<size_t>(boardId)];
                slot.record = std::move(record);
                slot.attemptedRequestId = 0;
            } else {
                log("High score upload was not started because retry data could not be persisted for board" +
                    std::to_string(boardId) + ".");
            }
            return;
        }

        log("High score retry storage is unavailable for board" + std::to_string(boardId) + ".");
        CSteamLeaderboardHelper* board = getBoard(boardId, true);
        if (!board) {
            return;
        }
        if (record.ugc.empty()) {
            board->sendScore(score);
        } else {
            board->sendScore(score, record.ugc.data(), record.ugc.size());
        }
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
        clearUgc();
        auto reference = entryReferences.find(guestEntryAddress);
        if (reference == entryReferences.end() || reference->second.boardId != selectedBoardId) {
            ugcSize = -1;
            log("Rejected UGC download for an unknown leaderboard entry.");
            return;
        }
        CSteamLeaderboardHelper* board = getBoard(selectedBoardId, false);
        LeaderboardEntry_t* entry = resolveEntry(reference->second);
        if (!board || !entry) {
            ugcSize = -1;
            return;
        }
        const uint64_t generation = ugcGeneration;
        board->downloadUGC(entry, [this, generation](const uint8_t* data, size_t size) {
            if (generation != ugcGeneration) {
                return;
            }
            if (!data || !size || size > ugcSizeLimit) {
                ugcData.clear();
                ugcSize = -1;
                return;
            }
            ugcData.assign(data, data + size);
            ugcSize = static_cast<int32_t>(size);
        });
    }

    uint32_t readUgcWord() const
    {
        const uint64_t offset = static_cast<uint64_t>(ugcReadIndex) * sizeof(uint32_t);
        if (offset + sizeof(uint32_t) > ugcData.size()) {
            return UINT32_MAX;
        }
        const size_t position = static_cast<size_t>(offset);
        return static_cast<uint32_t>(ugcData[position]) |
               (static_cast<uint32_t>(ugcData[position + 1]) << 8) |
               (static_cast<uint32_t>(ugcData[position + 2]) << 16) |
               (static_cast<uint32_t>(ugcData[position + 3]) << 24);
    }

    void appendUgcWord(uint32_t value)
    {
        ++ugcGeneration;
        if (ugcData.size() > ugcSizeLimit - sizeof(value)) {
            if (!ugcOverflow) {
                log("UGC buffer limit exceeded; append rejected.");
                ugcOverflow = true;
            }
            return;
        }
        ugcData.push_back(static_cast<uint8_t>(value));
        ugcData.push_back(static_cast<uint8_t>(value >> 8));
        ugcData.push_back(static_cast<uint8_t>(value >> 16));
        ugcData.push_back(static_cast<uint8_t>(value >> 24));
        ugcSize = static_cast<int32_t>(ugcData.size());
    }

    void clearUgc()
    {
        ++ugcGeneration;
        ugcReadIndex = 0;
        ugcSize = 0;
        ugcData.clear();
        ugcOverflow = false;
    }

    void setUgcSizeLimit(uint32_t size)
    {
        if (!DewpointUgc::isValidSizeLimit(size)) {
            log("Rejected invalid UGC size limit: " + std::to_string(size) + ".");
            return;
        }
        if (size == ugcSizeLimit) {
            return;
        }
        for (const auto& board : boards) {
            if (board && (board->isSendScoreBusy() || board->isDownloadBusyUGC())) {
                log("Rejected UGC size limit change while a UGC operation is in progress.");
                return;
            }
        }
        for (size_t index = 0; index < uploadSlots.size(); ++index) {
            if (uploadSlots[index].record && uploadSlots[index].record->ugc.size() > size) {
                uploadSlots[index] = {};
                pendingLoadDeferred[index] = true;
            }
        }
        ugcSizeLimit = size;
        highScoreStore.setUgcSizeLimit(size);
        for (auto& board : boards) {
            if (board) {
                board->setUgcSizeLimit(size);
            }
        }
        for (int boardId = 0; boardId < BOARD_COUNT; ++boardId) {
            if (pendingLoadDeferred[static_cast<size_t>(boardId)]) {
                loadPendingRecord(boardId);
            }
        }
        clearUgc();
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
        selectedBoardId = -1;
        sendUgc = false;
        guestEntryAddress = 0;
        achievementId.clear();
        achievementOverflow = false;
        clearUgc();
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
        impl->log("SteamAPI_Init failed; Steam features are disabled.");
        return false;
    }
    impl->steamInitialized = true;
    return true;
}

void DewpointRuntime::tick()
{
    if (impl->steamInitialized) {
        SteamAPI_RunCallbacks();
        impl->processPendingScores();
    }
}

bool DewpointRuntime::setHighScoreStorageDirectory(const std::string& directory)
{
    return impl->configureHighScoreStore(directory);
}

void DewpointRuntime::setFullscreenCallbacks(FullscreenSetter setter, FullscreenGetter getter)
{
    impl->fullscreenSetter = std::move(setter);
    impl->fullscreenGetter = std::move(getter);
    if (impl->fullscreenGetter) {
        impl->fullscreen = impl->fullscreenGetter();
    }
}

void DewpointRuntime::setButtonInputType(ButtonInputType type)
{
    impl->buttonInputType = type;
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
        case DpaIndexUgcSize: return static_cast<uint32_t>(impl->ugcSize);
        case DpaIndexUgcRead: return impl->readUgcWord();
        case DpaIndexUgcLimitSize: return impl->ugcSizeLimit;
        case DpaButtonA: return static_cast<uint32_t>(getButtonCharacters(impl->buttonInputType).a);
        case DpaButtonB: return static_cast<uint32_t>(getButtonCharacters(impl->buttonInputType).b);
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
    if (index == DpaAppVersion) {
        if (!impl->gba.writeGuestMemory(value, APP_VERSION, sizeof(APP_VERSION))) {
            impl->log("Rejected app version destination outside GBA work RAM.");
        }
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
            if (impl->steamInitialized && impl->selectedBoardId >= 0) {
                impl->getBoard(impl->selectedBoardId, true);
            }
            break;
        case DpaIndexSetUgcOption: impl->sendUgc = value != 0; break;
        case DpaIndexSendScore: impl->submitScore(impl->selectedBoardId, static_cast<int32_t>(value)); break;
        case DpaIndexBoardEntry: impl->guestEntryAddress = value; break;
        case DpaIndexBoardEntryGet: impl->writeEntry(static_cast<int32_t>(value)); break;
        case DpaIndexUgcClear: impl->clearUgc(); break;
        case DpaIndexUgcAppend: impl->appendUgcWord(value); break;
        case DpaIndexUgcDownload: impl->downloadUgc(); break;
        case DpaIndexUgcReadPtr: impl->ugcReadIndex = value; break;
        case DpaIndexUgcLimitSize: impl->setUgcSizeLimit(value); break;
        default: break;
    }
}

void DewpointRuntime::reset()
{
    impl->resetProtocol();
}
