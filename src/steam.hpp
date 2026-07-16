/**
 * Steamworks Helper
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include "steam_api.h"
#include <ctype.h>
#include <vector>
#include <string>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <functional>
#include <chrono>
#include <utility>
#include <cmath>
#include <thread>

#define PROTOCOL_VERSION "VR 1.0"
#define ROOM_ID_DEFAULT "$no_name"
static const char BASE28[] = "12346789ABCDE@GHJKMNPRTUVWXY";

class CSteam
{
  public:
    // --- static instance + NetworkingSockets callback bridge ---
    static inline CSteam* s_instance = nullptr;
    static void StaticConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info)
    {
        if (s_instance) {
            s_instance->onConnectionStatusChanged(info);
        }
    }

    enum class GameType {
        KoiKoi = 0
    };

    enum class RoundType {
        Single = 1,
        VeryShort = 2,
        Short = 4,
        Half = 6,
        Full = 12,
    };

    enum class MatchState {
        Idle = 0,
        Searching,
        Joining,
        Creating,
        WaitingForClient,
        LobbyEntered,
        Connecting,
        Completed,
        TimedOut
    };

    enum class LobbyAvailabilityCheckKind {
        QuickMatch = 0,
        FriendsMatch,
    };

    struct P2PConnectionInfo {
        bool isServer = false;
        int rounds;
        uint16 listenPort = 0;
        SteamNetworkingIdentity serverIdentity;
        HSteamListenSocket listenSocket = k_HSteamListenSocket_Invalid;
        HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
        CSteamID lobbyID;
        std::string failedReason{};
    };

  private:
    struct LobbyAvailabilityCheckState {
        bool active;
        bool delayingMiss;
        uint8_t result;
        std::chrono::steady_clock::time_point missDeadline;
        GameType game;
        RoundType round;
        ELobbyType lobbyType;
        std::string roomId;
    };

    struct LobbyDataRequestState {
        bool active;
        bool completed;
        bool success;
        CSteamID lobbyID;
    };

    bool initialized;
    bool overlay;
    InputAnalogActionHandle_t actMove;   // Left Stick as D-Pad (common)
    InputDigitalActionHandle_t actUp;    // D-Pad up (common)
    InputDigitalActionHandle_t actDown;  // D-Pad down (common)
    InputDigitalActionHandle_t actLeft;  // D-Pad left (common)
    InputDigitalActionHandle_t actRight; // D-Pad right (common)
    InputDigitalActionHandle_t actA;     // SW: A, XBOX: A, PS: Cross
    InputDigitalActionHandle_t actB;     // SW: B, XBOX: B, PS: Circle
    InputDigitalActionHandle_t actX;     // SW: X, XBOX: X, PS: Triangle
    InputDigitalActionHandle_t actY;     // SW: Y, XBOX: Y, PS: Square
    InputDigitalActionHandle_t actStart; // SW: +, XBOX: Menu, PS: options

    // Steam overlay callback（これは従来通り SteamAPI のコールバック）
    STEAM_CALLBACK_MANUAL(CSteam, onGameOverlayActivated, GameOverlayActivated_t, callbackGameOverlayActivated);
    CCallbackManual<CSteam, LobbyDataUpdate_t> callbackLobbyDataUpdate;

    void (*logger)(const char*);

    MatchState matchState;
    GameType matchGame;
    RoundType matchRound;
    ELobbyType matchLobbyType;
    int matchTimeoutSec;
    std::chrono::steady_clock::time_point matchDeadline;
    P2PConnectionInfo matchInfo;
    std::function<void(const P2PConnectionInfo&)> matchCallback;
    bool matchOwnsSockets;
    bool matchCompleting;
    bool matchRequestFromStartMatching;
    bool matchRoomIdIsNotDefault;
    std::string matchRoomId;
    CCallResult<CSteam, LobbyMatchList_t> callbackLobbyList;
    CCallResult<CSteam, LobbyMatchList_t> callbackQuickLobbyAvailabilityCheck;
    CCallResult<CSteam, LobbyMatchList_t> callbackFriendsLobbyAvailabilityCheck;
    CCallResult<CSteam, LobbyCreated_t> callbackLobbyCreated;
    CCallResult<CSteam, LobbyEnter_t> callbackLobbyEnter;
    std::vector<std::string> receiveQueue;
    size_t receiveQueueHead;
    LobbyAvailabilityCheckState quickLobbyAvailabilityCheck;
    LobbyAvailabilityCheckState friendsLobbyAvailabilityCheck;
    LobbyDataRequestState lobbyDataRequestState;

    inline void clearReceiveQueue()
    {
        this->receiveQueue.clear();
        this->receiveQueueHead = 0;
    }

    inline LobbyAvailabilityCheckState& lobbyAvailabilityCheckState(LobbyAvailabilityCheckKind kind)
    {
        return kind == LobbyAvailabilityCheckKind::QuickMatch ? this->quickLobbyAvailabilityCheck : this->friendsLobbyAvailabilityCheck;
    }

    inline CCallResult<CSteam, LobbyMatchList_t>& lobbyAvailabilityCheckCallback(LobbyAvailabilityCheckKind kind)
    {
        return kind == LobbyAvailabilityCheckKind::QuickMatch ? this->callbackQuickLobbyAvailabilityCheck : this->callbackFriendsLobbyAvailabilityCheck;
    }

    inline void clearLobbyAvailabilityCheckState(LobbyAvailabilityCheckKind kind)
    {
        auto& state = this->lobbyAvailabilityCheckState(kind);
        state.active = false;
        state.delayingMiss = false;
        state.result = 0;
        state.missDeadline = std::chrono::steady_clock::time_point{};
        state.game = GameType::KoiKoi;
        state.round = RoundType::Single;
        state.lobbyType = k_ELobbyTypePublic;
        state.roomId = ROOM_ID_DEFAULT;
    }

    inline void clearLobbyDataRequestState()
    {
        this->lobbyDataRequestState.active = false;
        this->lobbyDataRequestState.completed = false;
        this->lobbyDataRequestState.success = false;
        this->lobbyDataRequestState.lobbyID = k_steamIDNil;
    }

  public:
    void putlog(const char* msg, ...)
    {
        if (!logger) {
            return;
        }
        char buf[1024];
        va_list args;
        va_start(args, msg);
        vsnprintf(buf, sizeof(buf), msg, args);
        va_end(args);
        logger(buf);
    }

    enum class ControllerType {
        NotConnected = 0,
        XBOX,
        NintendoSwitch,
        PlayStation,
    };
    struct ButtonState {
        bool connected;
        ControllerType type;
        bool up;
        bool down;
        bool left;
        bool right;
        bool a;
        bool b;
        bool x;
        bool y;
        bool start;
    } buttonState;

    CSteam() : logger(nullptr)
    {
        CSteam::s_instance = this;

        this->initialized = false;
        this->overlay = false;
        this->deactivate();
        this->clearButtonState();
        this->matchState = MatchState::Idle;
        this->matchTimeoutSec = 0;
        this->matchInfo = P2PConnectionInfo{};
        this->matchCallback = nullptr;
        this->matchOwnsSockets = false;
        this->matchCompleting = false;
        this->matchRequestFromStartMatching = false;
        this->matchRoomIdIsNotDefault = false;
        this->matchRoomId = ROOM_ID_DEFAULT;
        this->clearLobbyAvailabilityCheckState(LobbyAvailabilityCheckKind::QuickMatch);
        this->clearLobbyAvailabilityCheckState(LobbyAvailabilityCheckKind::FriendsMatch);
        this->clearLobbyDataRequestState();
        this->clearReceiveQueue();
    }

    ~CSteam()
    {
        this->cancelLobbyAvailabilityChecks();
        this->cancelMatchmakingRequests();
        this->closeMatchmakingSockets(true);
        if (this->initialized) {
            putlog("Teminating Steam...");
            SteamInput()->Shutdown();
            SteamAPI_Shutdown();
        }
    }

    void setLoggger(void (*logger)(const char* msg))
    {
        this->logger = logger;
    }

    bool isSteamOverlayEnabled()
    {
        if (!SteamAPI_IsSteamRunning())
            return false; // Steamクライアントが起動していない

        if (!SteamUtils())
            return false; // API未初期化

        return SteamUtils()->IsOverlayEnabled();
    }

    bool isRunningOnSteamDeck()
    {
        return SteamUtils() ? SteamUtils()->IsSteamRunningOnSteamDeck() : false;
    }

    static bool isEnabledWindowModo()
    {
        return getenv("SteamTenfoot") == nullptr;
    }

    bool init()
    {
        putlog("Initializing Steam.");
        if (!SteamAPI_Init()) {
            putlog("SteamAPI_Init failed");
        } else {
            auto* utils = SteamNetworkingUtils();
            auto* input = SteamInput();
            utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable, 0);      // ICE 無効
            utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_SDR_Penalty, 0);     // リレー優先（最小ペナルティ）
            utils->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Penalty, 10000); // ICE があっても選ばれないよう巨大ペナルティ
            utils->SetGlobalCallback_SteamNetConnectionStatusChanged(&CSteam::StaticConnectionStatusChanged);
            utils->InitRelayNetworkAccess();
            SteamNetworkingSockets()->InitAuthentication();
            callbackGameOverlayActivated.Register(this, &CSteam::onGameOverlayActivated);
            callbackLobbyDataUpdate.Register(this, &CSteam::onLobbyDataUpdated);
            auto* apps = SteamApps();
            if (input && apps) {
                char installDir[1024];
                AppId_t appId = SteamUtils() ? SteamUtils()->GetAppID() : 0;
                if (apps->GetAppInstallDir(appId, installDir, sizeof(installDir)) > 0) {
                    std::string manifestPath(installDir);
                    if (!manifestPath.empty() && manifestPath.back() != '/' && manifestPath.back() != '\\') {
                        manifestPath += '/';
                    }
                    manifestPath += "action_manifest.vdf";
                    if (input->SetInputActionManifestFilePath(manifestPath.c_str())) {
                        putlog("SteamInput action manifest: %s", manifestPath.c_str());
                    } else {
                        putlog("SetInputActionManifestFilePath failed: %s", manifestPath.c_str());
                    }
                } else {
                    putlog("GetAppInstallDir failed.");
                }
            } else {
                putlog("SteamInput or SteamApps unavailable.");
            }
            if (!input || !input->Init(true)) {
                putlog("SteamInput::Init failed!");
            } else {
                putlog("SteamInput initialized.");
                this->initialized = true;
            }
        }
        return this->initialized;
    }

    void enableNetworkLogging()
    {
        SteamNetworkingUtils()->SetDebugOutputFunction(
            k_ESteamNetworkingSocketsDebugOutputType_Verbose,
            [](ESteamNetworkingSocketsDebugOutputType eType, const char* pszMsg) {
                s_instance->putlog("NWLOG[%d] %s", (int)eType, pszMsg);
            });
    }

    // アチーブメントをアンロック
    void unlockAchievement(const char* name)
    {
        putlog("Unlock achievement: %s", name);
        if (!this->initialized) {
            return;
        }
        if (!SteamUserStats()->SetAchievement(name)) {
            putlog("SteamUserStats::SetAchievement(%s) failed!", name);
        } else {
            if (!SteamUserStats()->StoreStats()) {
                putlog("SteamUserStats::StoreStats failed!");
            }
        }
    }

    bool openUrl(const char* url)
    {
        if (SteamFriends()) {
            SteamFriends()->ActivateGameOverlayToWebPage(url, k_EActivateGameOverlayToWebPageMode_Default);
            return true;
        } else {
            // 初期化されていない / Steamクライアント外で起動 等
            putlog("SteamFriends() is null. SteamAPI not available.");
            return false;
        }
    }

    std::vector<uint32_t> getOwnAvatar()
    {
        return this->getAvatar(SteamUser()->GetSteamID());
    }

    std::vector<uint32_t> getOpponentAvatar()
    {
        auto* networking = SteamNetworkingSockets();
        if (networking && this->matchInfo.connection != k_HSteamNetConnection_Invalid) {
            SteamNetConnectionInfo_t info{};
            if (networking->GetConnectionInfo(this->matchInfo.connection, &info) &&
                info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
                return this->getAvatar(info.m_identityRemote.GetSteamID());
            }
        }
        return {};
    }

    void runLoop()
    {
        if (initialized) {
            SteamInput()->RunFrame();
            SteamAPI_RunCallbacks();
            // P2P 接続系のイベントは SteamNetworkingSockets の RunCallbacks でディスパッチされる
            SteamNetworkingSockets()->RunCallbacks();
            this->updateButtonState();
            this->tickMatchmaking();
        }
    }

    bool p2pSend(const std::string& msg)
    {
        putlog("p2pSend: %s", msg.substr(0, 2).c_str());
        auto* networking = SteamNetworkingSockets();
        if (!networking || this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
            setFailReason("Invalid connection");
            return false;
        }
        if (msg.size() >= 0xFFFF) {
            setFailReason("Invalid message size");
            return false;
        }
        const uint16 payloadSize = static_cast<uint16>(msg.size() + 1);
        const uint32 totalSize = static_cast<uint32>(payloadSize) + 2;
        std::vector<unsigned char> buffer(static_cast<size_t>(totalSize));
        buffer[0] = static_cast<unsigned char>((payloadSize >> 8) & 0xFF);
        buffer[1] = static_cast<unsigned char>(payloadSize & 0xFF);
        memcpy(buffer.data() + 2, msg.data(), msg.size());
        buffer[2 + msg.size()] = '\0';
        EResult result = networking->SendMessageToConnection(
            this->matchInfo.connection,
            buffer.data(),
            totalSize,
            k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle,
            nullptr);
        return result == k_EResultOK;
    }

    bool p2pReceive(int timeoutMs, const std::function<bool(const std::string&)>& callback)
    {
        if (!callback) {
            setFailReason("Logic error: no callback");
            return false;
        }
        auto* networking = SteamNetworkingSockets();
        if (!networking || this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
            setFailReason("SteamNetworkingSockets unavailable");
            return false;
        }
        auto invokeCallback = [&](std::string&& payload) -> bool {
            putlog("p2pReceive: %s", payload.substr(0, 2).c_str());
            if (!callback(payload)) {
                setFailReason("Callback failed");
                networking->CloseConnection(
                    this->matchInfo.connection,
                    k_ESteamNetConnectionEnd_App_Generic,
                    nullptr,
                    false);
                this->matchInfo.connection = k_HSteamNetConnection_Invalid;
                this->clearReceiveQueue();
                return false;
            }
            return true;
        };
        if (this->receiveQueueHead < this->receiveQueue.size()) {
            std::string payload = std::move(this->receiveQueue[this->receiveQueueHead]);
            ++this->receiveQueueHead;
            if (this->receiveQueueHead >= this->receiveQueue.size()) {
                this->clearReceiveQueue();
            }
            return invokeCallback(std::move(payload));
        }
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
        const bool waitForMessage = timeoutMs > 0;
        while (true) {
            SteamNetworkingMessage_t* message = nullptr;
            networking->RunCallbacks();
            int received = networking->ReceiveMessagesOnConnection(this->matchInfo.connection, &message, 1);
            if (received < 0) {
                setFailReason("ReceiveMessagesOnConnection failed");
                return false;
            }
            if (received > 0 && message) {
                bool parsed = false;
                std::vector<std::string> payloads;
                const unsigned char* data = reinterpret_cast<const unsigned char*>(message->m_pData);
                const size_t size = static_cast<size_t>(message->m_cbSize);
                if (size >= 3 && data) {
                    size_t offset = 0;
                    parsed = true;
                    while (parsed && offset + 2 <= size) {
                        const uint16 payloadSize = static_cast<uint16>((static_cast<uint16>(data[offset]) << 8) |
                                                                       data[offset + 1]);
                        offset += 2;
                        if (payloadSize == 0 || offset + payloadSize > size) {
                            parsed = false;
                            break;
                        }
                        const char* payloadPtr = reinterpret_cast<const char*>(data + offset);
                        if (payloadPtr[payloadSize - 1] != '\0') {
                            parsed = false;
                            break;
                        }
                        payloads.emplace_back(payloadPtr, payloadSize - 1);
                        offset += payloadSize;
                    }
                    if (parsed && offset != size) {
                        parsed = false;
                    }
                }
                message->Release();
                if (!parsed || payloads.empty()) {
                    networking->CloseConnection(
                        this->matchInfo.connection,
                        k_ESteamNetConnectionEnd_App_Generic,
                        nullptr,
                        false);
                    this->matchInfo.connection = k_HSteamNetConnection_Invalid;
                    this->clearReceiveQueue();
                    return false;
                }
                if (payloads.size() > 1) {
                    if (!this->receiveQueue.empty()) {
                        this->clearReceiveQueue();
                    }
                    for (size_t i = 1; i < payloads.size(); ++i) {
                        this->receiveQueue.emplace_back(std::move(payloads[i]));
                    }
                    this->receiveQueueHead = 0;
                }
                return invokeCallback(std::move(payloads.front()));
            }
            if (timeoutMs == 0 && received == 0) {
                return callback(std::string{});
            }
            if (!waitForMessage || std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        setFailReason("Receive timeout");
        return false;
    }

    inline bool isOverlay() { return this->overlay; }

    uint64_t getOpponentSteamId()
    {
        auto* networking = SteamNetworkingSockets();
        if (networking && this->matchInfo.connection != k_HSteamNetConnection_Invalid) {
            SteamNetConnectionInfo_t info{};
            if (networking->GetConnectionInfo(this->matchInfo.connection, &info) &&
                info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
                return info.m_identityRemote.GetSteamID().ConvertToUint64();
            }
        }
        return 0;
    }

    std::string getOpponentPersonaName()
    {
        uint64_t steamId64 = this->getOpponentSteamId();
        auto* friends = SteamFriends();
        if (steamId64 == 0 || !friends) {
            return "";
        }
        CSteamID id;
        id.SetFromUint64(steamId64);
        const char* rawName = friends->GetFriendPersonaName(id);
        if (!rawName) {
            return "";
        }
        std::string sanitized;
        sanitized.reserve(19);
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(rawName); *p; ++p) {
            unsigned char c = *p;
            if (c < 0x20) {
                sanitized.push_back('?');
            } else if (c < 0x80) {
                sanitized.push_back(static_cast<char>(c));
            } else {
                size_t len = 1;
                if ((c & 0xE0) == 0xC0) {
                    len = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    len = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    len = 4;
                }
                sanitized.push_back('?');
                for (size_t i = 1; i < len && p[1]; ++i, ++p) {
                    if ((p[1] & 0xC0) != 0x80) {
                        break;
                    }
                }
            }
            if (sanitized.size() >= 20) {
                break;
            }
        }
        if (sanitized.size() >= 20) {
            sanitized.resize(19);
        }
        return sanitized;
    }

    void openSteamProfile(uint64_t steamId)
    {
        if (steamId == 0 || !isSteamOverlayEnabled()) {
            return;
        }
        ISteamFriends* friends = SteamFriends();
        if (!friends) {
            return;
        }
        CSteamID target;
        target.SetFromUint64(steamId);
        if (!target.IsValid()) {
            putlog("Invalid steam id: %llu", steamId);
            return;
        }
        putlog("Open profile: %s", friends->GetFriendPersonaName(target));
        friends->ActivateGameOverlayToUser("steamid", target);
    }

    int showKeyConfig(void)
    {
        if (!isSteamOverlayEnabled()) {
            return 1;
        }
        SteamFriends()->ActivateGameOverlay("settings");
        return 2;
    }

    std::string getRoomId()
    {
        if (!this->matchInfo.lobbyID.IsValid()) {
            return "";
        }
        return this->matchRoomId;
    }

    int joinLobby(std::string roomId,
                  int timeoutSec,
                  std::function<void(const P2PConnectionInfo& info)> callback)
    {
        if (!this->initialized) {
            putlog("joinLobby: Steam not initialized.");
            return -1;
        }
        if (this->matchState != MatchState::Idle && this->matchState != MatchState::TimedOut) {
            putlog("joinLobby: already running.");
            return -2;
        }
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking || !SteamNetworkingSockets() || !SteamUser() || !SteamUtils()) {
            putlog("joinLobby: Steam interfaces unavailable.");
            return -3;
        }
        if (roomId.empty()) {
            putlog("joinLobby: room id is empty.");
            return -4;
        }
        this->cancelLobbyAvailabilityChecks();
        this->cancelMatchmakingRequests();
        this->closeMatchmakingSockets(true);

        this->matchCallback = std::move(callback);
        this->matchGame = GameType::KoiKoi;
        this->matchRound = RoundType::Full; // Room 作成者が指定する RoundType が用いられる
        this->matchLobbyType = k_ELobbyTypePublic;
        this->matchTimeoutSec = timeoutSec <= 0 ? 20 : timeoutSec;
        this->matchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(this->matchTimeoutSec);
        this->matchInfo = P2PConnectionInfo{};
        this->matchInfo.rounds = 0;
        this->matchInfo.listenPort = this->chooseVirtualPort();
        this->matchInfo.serverIdentity.Clear();
        this->matchInfo.serverIdentity.SetSteamID(SteamUser()->GetSteamID());
        this->matchOwnsSockets = false;
        this->matchRequestFromStartMatching = false;
        this->matchRoomIdIsNotDefault = (roomId != ROOM_ID_DEFAULT);
        this->matchRoomId = roomId;
        matchmaking->AddRequestLobbyListDistanceFilter(k_ELobbyDistanceFilterWorldwide);
        matchmaking->AddRequestLobbyListFilterSlotsAvailable(1);
        matchmaking->AddRequestLobbyListStringFilter("room_id", roomId.c_str(), k_ELobbyComparisonEqual);
        SteamAPICall_t call = matchmaking->RequestLobbyList();
        if (call == k_uAPICallInvalid) {
            putlog("startMatching: RequestLobbyList failed.");
            this->failMatchmaking("RequestLobbyList failed");
            return -6;
        }
        this->matchState = MatchState::Searching;
        putlog("joinLobby: roomId=%s", roomId.c_str());
        this->callbackLobbyList.Set(call, this, &CSteam::onLobbyJoinList);
        return 0;
    }

    std::string getLastCommunicationErrorReason()
    {
        return this->matchInfo.failedReason;
    }

    int startLobbyAvailabilityCheck(
        LobbyAvailabilityCheckKind kind,
        ELobbyType lobbyType,
        ELobbyDistanceFilter filter,
        GameType game,
        RoundType round,
        const char* roomId)
    {
        auto& state = this->lobbyAvailabilityCheckState(kind);
        if (!this->initialized) {
            putlog("startLobbyAvailabilityCheck: Steam not initialized.");
            state.result = 0;
            return -1;
        }
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            putlog("startLobbyAvailabilityCheck: SteamMatchmaking unavailable.");
            state.result = 0;
            return -2;
        }

        this->cancelLobbyAvailabilityCheck(kind);

        const std::string requestedRoomId = roomId ? roomId : ROOM_ID_DEFAULT;
        state.game = game;
        state.round = round;
        state.lobbyType = lobbyType;
        state.roomId = requestedRoomId;
        if (lobbyType == k_ELobbyTypeFriendsOnly) {
            CSteamID lobbyID;
            state.active = false;
            state.delayingMiss = false;
            state.result = 255;
            if (this->findFriendLobby(game, round, lobbyType, requestedRoomId, &lobbyID)) {
                state.result = 1;
            } else {
                state.delayingMiss = true;
                state.missDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            }
            putlog("startLobbyAvailabilityCheck[%d]: friends match result=%u", (int)kind, state.result);
            return 0;
        }

        matchmaking->AddRequestLobbyListDistanceFilter(filter);
        matchmaking->AddRequestLobbyListFilterSlotsAvailable(1);
        matchmaking->AddRequestLobbyListStringFilter("game_type", this->gameTypeToString(game).c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("round_type", this->roundTypeToString(round).c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("room_id", requestedRoomId.c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("lobby_type", this->lobbyTypeToString(lobbyType).c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("joinable", "2", k_ELobbyComparisonEqual);

        SteamAPICall_t call = matchmaking->RequestLobbyList();
        if (call == k_uAPICallInvalid) {
            putlog("startLobbyAvailabilityCheck: RequestLobbyList failed.");
            state.result = 0;
            return -3;
        }

        state.active = true;
        state.delayingMiss = false;
        state.result = 255;
        state.missDeadline = std::chrono::steady_clock::time_point{};
        if (kind == LobbyAvailabilityCheckKind::QuickMatch) {
            this->callbackQuickLobbyAvailabilityCheck.Set(call, this, &CSteam::onQuickLobbyAvailabilityCheck);
        } else {
            this->callbackFriendsLobbyAvailabilityCheck.Set(call, this, &CSteam::onFriendsLobbyAvailabilityCheck);
        }
        putlog("startLobbyAvailabilityCheck[%d]: requesting lobby list...", (int)kind);
        return 0;
    }

    uint32_t getLobbyAvailabilityCheckResult(LobbyAvailabilityCheckKind kind)
    {
        auto& state = this->lobbyAvailabilityCheckState(kind);
        if (state.delayingMiss &&
            std::chrono::steady_clock::now() >= state.missDeadline) {
            state.delayingMiss = false;
            state.result = 0;
        }
        return state.result;
    }

    void cancelLobbyAvailabilityCheck(LobbyAvailabilityCheckKind kind)
    {
        this->lobbyAvailabilityCheckCallback(kind).Cancel();
        this->clearLobbyAvailabilityCheckState(kind);
    }

    void cancelLobbyAvailabilityChecks()
    {
        this->cancelLobbyAvailabilityCheck(LobbyAvailabilityCheckKind::QuickMatch);
        this->cancelLobbyAvailabilityCheck(LobbyAvailabilityCheckKind::FriendsMatch);
    }

    int startMatching(
        ELobbyType lobbyType,
        ELobbyDistanceFilter filter,
        GameType game,
        RoundType round,
        const char* roomId,
        int timeoutSec,
        std::function<void(const P2PConnectionInfo& info)> callback)
    {
        if (!this->initialized) {
            putlog("startMatching: Steam not initialized.");
            return -1;
        }
        if (this->matchState != MatchState::Idle) {
            putlog("startMatching: already running.");
            return -2;
        }
        if (!callback) {
            putlog("startMatching: callback is required.");
            return -3;
        }
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking || !SteamNetworkingSockets() || !SteamUser() || !SteamUtils()) {
            putlog("startMatching: Steam interfaces unavailable.");
            return -4;
        }
        this->cancelLobbyAvailabilityChecks();
        this->cancelMatchmakingRequests();
        this->closeMatchmakingSockets(true);

        this->matchCallback = std::move(callback);
        this->matchGame = game;
        this->matchRound = round;
        this->matchLobbyType = lobbyType;
        this->matchTimeoutSec = timeoutSec <= 0 ? 20 : timeoutSec;
        this->matchDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(this->matchTimeoutSec);
        this->matchInfo = P2PConnectionInfo{};
        this->matchInfo.rounds = static_cast<int>(round);
        this->matchInfo.listenPort = this->chooseVirtualPort();
        this->matchInfo.serverIdentity.Clear();
        this->matchInfo.serverIdentity.SetSteamID(SteamUser()->GetSteamID());
        this->matchOwnsSockets = false;
        this->matchRequestFromStartMatching = true;
        this->matchRoomId = roomId ? roomId : ROOM_ID_DEFAULT;
        this->matchRoomIdIsNotDefault = (this->matchRoomId != ROOM_ID_DEFAULT);

        if (lobbyType == k_ELobbyTypeFriendsOnly) {
            CSteamID lobbyID;
            if (this->findFriendLobby(game, round, lobbyType, this->matchRoomId, &lobbyID)) {
                return this->beginJoinLobby(lobbyID, "startMatching");
            }
            return this->beginCreateLobby("startMatching");
        }

        matchmaking->AddRequestLobbyListDistanceFilter(filter);
        matchmaking->AddRequestLobbyListFilterSlotsAvailable(1);
        matchmaking->AddRequestLobbyListStringFilter("game_type", this->gameTypeToString(game).c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("round_type", this->roundTypeToString(round).c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("room_id", this->matchRoomId.c_str(), k_ELobbyComparisonEqual);
        matchmaking->AddRequestLobbyListStringFilter("lobby_type", this->lobbyTypeToString(lobbyType).c_str(), k_ELobbyComparisonEqual);

        SteamAPICall_t call = matchmaking->RequestLobbyList();
        if (call == k_uAPICallInvalid) {
            putlog("startMatching: RequestLobbyList failed.");
            this->failMatchmaking("RequestLobbyList failed");
            return -6;
        }
        this->matchState = MatchState::Searching;
        this->callbackLobbyList.Set(call, this, &CSteam::onLobbyMatchList);
        putlog("startMatching: requesting lobby list...");
        return 0;
    }

    void disconnect()
    {
        putlog("Disconnecting...");
        auto* matchmaking = SteamMatchmaking();
        if (matchmaking && this->matchInfo.lobbyID.IsValid()) {
            matchmaking->LeaveLobby(this->matchInfo.lobbyID);
            this->matchInfo.lobbyID = k_steamIDNil;
        }
        if (this->matchState != MatchState::Idle) {
            this->failMatchmaking("Disconnect requested");
            return;
        }
        this->closeMatchmakingSockets(true);
    }

    bool isOnline()
    {
        if (this->matchState != MatchState::Idle && this->matchState != MatchState::TimedOut) {
            if (this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
                return true;
            }
            auto* networking = SteamNetworkingSockets();
            if (!networking) {
                return true;
            }
            SteamNetConnectionInfo_t info{};
            if (!networking->GetConnectionInfo(this->matchInfo.connection, &info)) {
                return true;
            }
            if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer) {
                setFailReason("Closed by peer");
            } else if (info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                setFailReason("Local I/O error");
            }
            return info.m_eState != k_ESteamNetworkingConnectionState_ClosedByPeer &&
                   info.m_eState != k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
        }
        if (this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
            return false;
        }
        auto* networking = SteamNetworkingSockets();
        if (!networking) {
            setFailReason("SteamNetworkingSockets unavailable");
            return false;
        }
        SteamNetConnectionInfo_t info{};
        if (!networking->GetConnectionInfo(this->matchInfo.connection, &info)) {
            setFailReason("GetConnectionInfo failed");
            return false;
        }
        if (info.m_eState != k_ESteamNetworkingConnectionState_Connected) {
            if (info.m_eState == k_ESteamNetworkingConnectionState_ClosedByPeer) {
                setFailReason("Closed by peer");
            } else if (info.m_eState == k_ESteamNetworkingConnectionState_ProblemDetectedLocally) {
                setFailReason("Local I/O error");
            } else {
                setFailReason("Not connected");
            }
        }
        return info.m_eState == k_ESteamNetworkingConnectionState_Connected;
    }

    void openStorePage()
    {
        if (!SteamAPI_IsSteamRunning() || !SteamAPI_Init()) {
            putlog("Steam not running");
            return;
        }
        if (!SteamUtils()->IsOverlayEnabled()) {
            putlog("Steam overlay is not enabled");
            return;
        }
        auto steamFriends = SteamFriends();
        if (!steamFriends) {
            putlog("SteamFriends API is not available");
            return;
        }
        steamFriends->ActivateGameOverlayToStore(4161340, k_EOverlayToStoreFlag_None);
    }

  private:
    std::vector<uint32_t> getAvatar(const CSteamID& steamId)
    {
        std::vector<uint32_t> r;
        int a = SteamFriends()->GetSmallFriendAvatar(steamId);
        if (a <= 0) return r;
        uint32_t w, h;
        if (!SteamUtils()->GetImageSize(a, &w, &h)) {
            return r;
        }
        std::vector<uint32_t> tmp(w * h);
        if (!SteamUtils()->GetImageRGBA(a, reinterpret_cast<uint8_t*>(tmp.data()), tmp.size() * 4)) {
            return r;
        }
        putlog("Avatar received: ID=%u, width=%d, height=%d", steamId.GetAccountID(), w, h);
        r.resize(tmp.size());
        for (size_t i = 0; i < tmp.size(); ++i) {
            uint32_t v = tmp[i];
            uint8_t r8 = (v >> 0) & 0xFF;
            uint8_t g8 = (v >> 8) & 0xFF;
            uint8_t b8 = (v >> 16) & 0xFF;
            uint8_t a8 = (v >> 24) & 0xFF;
            r[i] = (uint32_t(r8) << 24) | (uint32_t(g8) << 16) | (uint32_t(b8) << 8) | a8;
        }
        return r;
    }

    std::string gameTypeToString(GameType game) const
    {
        switch (game) {
            case GameType::KoiKoi:
            default:
                return "koikoi";
        }
    }

    std::string roundTypeToString(RoundType round) const
    {
        switch (round) {
            case RoundType::Single: return "single";
            case RoundType::VeryShort: return "very_short";
            case RoundType::Short: return "short";
            case RoundType::Half: return "half";
            case RoundType::Full: return "full";
            default: return "single";
        }
    }

    std::string lobbyTypeToString(ELobbyType lobbyType) const
    {
        switch (lobbyType) {
            case k_ELobbyTypeFriendsOnly: return "friends";
            case k_ELobbyTypePublic:
            default:
                return "public";
        }
    }

    uint16 chooseVirtualPort() const
    {
        auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count();
        return static_cast<uint16>((ticks % 40000) + 2000);
    }

    bool requestLobbyDataSync(CSteamID lobbyID, int timeoutMs = 1500)
    {
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking || !lobbyID.IsValid()) {
            return false;
        }
        this->clearLobbyDataRequestState();
        this->lobbyDataRequestState.active = true;
        this->lobbyDataRequestState.lobbyID = lobbyID;
        if (!matchmaking->RequestLobbyData(lobbyID)) {
            this->clearLobbyDataRequestState();
            return false;
        }
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs > 0 ? timeoutMs : 0);
        while (!this->lobbyDataRequestState.completed && std::chrono::steady_clock::now() < deadline) {
            SteamAPI_RunCallbacks();
            SteamNetworkingSockets()->RunCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const bool success = this->lobbyDataRequestState.completed && this->lobbyDataRequestState.success;
        this->clearLobbyDataRequestState();
        return success;
    }

    bool isExpectedLobby(CSteamID lobbyID, GameType game, RoundType round, ELobbyType lobbyType, const std::string& roomId)
    {
        auto* matchmaking = SteamMatchmaking();
        auto* user = SteamUser();
        if (!matchmaking || !user || !lobbyID.IsValid()) {
            return false;
        }
        const CSteamID me = user->GetSteamID();
        const CSteamID owner = matchmaking->GetLobbyOwner(lobbyID);
        if (owner == me) {
            putlog("Ignored own lobby %llu during availability check.", lobbyID.ConvertToUint64());
            return false;
        }
        const char* lobbyGame = matchmaking->GetLobbyData(lobbyID, "game_type");
        const char* lobbyRound = matchmaking->GetLobbyData(lobbyID, "round_type");
        const char* lobbyRoom = matchmaking->GetLobbyData(lobbyID, "room_id");
        const char* lobbyKind = matchmaking->GetLobbyData(lobbyID, "lobby_type");
        const char* joinable = matchmaking->GetLobbyData(lobbyID, "joinable");
        const char* encodedIdentity = matchmaking->GetLobbyData(lobbyID, "p2p_server_addr");
        const char* portValue = matchmaking->GetLobbyData(lobbyID, "p2p_server_port");
        if (!lobbyGame || strcmp(lobbyGame, this->gameTypeToString(game).c_str()) != 0) {
            return false;
        }
        if (!lobbyRound || strcmp(lobbyRound, this->roundTypeToString(round).c_str()) != 0) {
            return false;
        }
        if (!lobbyRoom || strcmp(lobbyRoom, roomId.c_str()) != 0) {
            return false;
        }
        if (!lobbyKind || strcmp(lobbyKind, this->lobbyTypeToString(lobbyType).c_str()) != 0) {
            return false;
        }
        if (!joinable || strcmp(joinable, "2") != 0) {
            return false;
        }
        if (!encodedIdentity || !encodedIdentity[0] || !portValue || !portValue[0]) {
            putlog("Ignored unusable lobby %llu (missing P2P metadata).", lobbyID.ConvertToUint64());
            return false;
        }
        return true;
    }

    bool findFriendLobby(GameType game, RoundType round, ELobbyType lobbyType, const std::string& roomId, CSteamID* outLobbyId)
    {
        auto* friends = SteamFriends();
        auto* utils = SteamUtils();
        if (!friends || !utils) {
            return false;
        }
        const AppId_t appId = utils->GetAppID();
        const int friendCount = friends->GetFriendCount(k_EFriendFlagImmediate);
        for (int i = 0; i < friendCount; ++i) {
            const CSteamID friendId = friends->GetFriendByIndex(i, k_EFriendFlagImmediate);
            if (!friendId.IsValid()) {
                continue;
            }
            FriendGameInfo_t gameInfo{};
            if (!friends->GetFriendGamePlayed(friendId, &gameInfo)) {
                continue;
            }
            if (gameInfo.m_gameID.AppID() != appId || !gameInfo.m_steamIDLobby.IsValid()) {
                continue;
            }
            if (!this->requestLobbyDataSync(gameInfo.m_steamIDLobby)) {
                putlog("Ignored friend lobby %llu (RequestLobbyData failed).", gameInfo.m_steamIDLobby.ConvertToUint64());
                continue;
            }
            if (!this->isExpectedLobby(gameInfo.m_steamIDLobby, game, round, lobbyType, roomId)) {
                continue;
            }
            if (outLobbyId) {
                *outLobbyId = gameInfo.m_steamIDLobby;
            }
            return true;
        }
        return false;
    }

    int beginJoinLobby(CSteamID lobbyID, const char* context)
    {
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            this->failMatchmaking("SteamMatchmaking unavailable");
            return -4;
        }
        this->matchInfo.lobbyID = lobbyID;
        SteamAPICall_t call = matchmaking->JoinLobby(lobbyID);
        if (call == k_uAPICallInvalid) {
            this->failMatchmaking("JoinLobby failed");
            return -6;
        }
        this->matchState = MatchState::Joining;
        this->callbackLobbyEnter.Set(call, this, &CSteam::onLobbyEnter);
        putlog("%s: joining lobby %llu", context, lobbyID.ConvertToUint64());
        return 0;
    }

    int beginCreateLobby(const char* context)
    {
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            this->failMatchmaking("SteamMatchmaking unavailable");
            return -4;
        }
        SteamAPICall_t call = matchmaking->CreateLobby(this->matchLobbyType, 2);
        if (call == k_uAPICallInvalid) {
            this->failMatchmaking("CreateLobby failed");
            return -6;
        }
        this->matchState = MatchState::Creating;
        this->callbackLobbyCreated.Set(call, this, &CSteam::onLobbyCreated);
        putlog("%s: creating new lobby", context);
        return 0;
    }

    bool findUsableLobby(
        LobbyMatchList_t* list,
        GameType game,
        RoundType round,
        ELobbyType lobbyType,
        const std::string& roomId,
        CSteamID* outLobbyId)
    {
        if (!list || list->m_nLobbiesMatching <= 0) {
            return false;
        }
        auto* matchmaking = SteamMatchmaking();
        auto* user = SteamUser();
        if (!matchmaking || !user) {
            return false;
        }
        CSteamID me = user->GetSteamID();
        for (uint32 i = 0; i < list->m_nLobbiesMatching; i++) {
            CSteamID lobbyID = matchmaking->GetLobbyByIndex(i);
            if (!lobbyID.IsValid()) {
                continue;
            }
            if (matchmaking->GetLobbyOwner(lobbyID) == me) {
                putlog("Ignored own lobby %llu during availability check.", lobbyID.ConvertToUint64());
                continue;
            }
            if (!this->isExpectedLobby(lobbyID, game, round, lobbyType, roomId)) {
                continue;
            }
            if (outLobbyId) {
                *outLobbyId = lobbyID;
            }
            return true;
        }
        return false;
    }

    void tickMatchmaking()
    {
        if (this->matchState == MatchState::Idle || this->matchTimeoutSec <= 0) {
            return;
        }
        if (std::chrono::steady_clock::now() > this->matchDeadline) {
            this->failMatchmaking("Matchmaking timeout", true);
        }
    }

    void closeMatchmakingSockets(bool force)
    {
        if (!force && !this->matchOwnsSockets) {
            return;
        }
        auto* networking = SteamNetworkingSockets();
        if (networking) {
            if (this->matchInfo.connection != k_HSteamNetConnection_Invalid) {
                networking->CloseConnection(
                    this->matchInfo.connection,
                    k_ESteamNetConnectionEnd_App_Generic,
                    nullptr,
                    false);
            }
            if (this->matchInfo.listenSocket != k_HSteamListenSocket_Invalid) {
                networking->CloseListenSocket(this->matchInfo.listenSocket);
            }
        }
        this->matchInfo.connection = k_HSteamNetConnection_Invalid;
        this->matchInfo.listenSocket = k_HSteamListenSocket_Invalid;
        this->matchOwnsSockets = false;
        this->clearReceiveQueue();
    }

    void cancelMatchmakingRequests()
    {
        this->callbackLobbyList.Cancel();
        this->callbackLobbyCreated.Cancel();
        this->callbackLobbyEnter.Cancel();
    }

    void setFailReason(const char* reason)
    {
        if (reason && this->matchInfo.failedReason.empty()) {
            this->matchInfo.failedReason = reason;
        }
    }

    void failMatchmaking(const char* reason, bool timedOut = false)
    {
        if (reason) {
            putlog("Matchmaking failed: %s", reason);
            setFailReason(reason);
        }
        if (timedOut) {
            this->matchState = MatchState::TimedOut;
        }
        this->cancelMatchmakingRequests();
        this->closeMatchmakingSockets(true);
        this->matchCallback = nullptr;
        this->matchTimeoutSec = 0;
        this->matchRequestFromStartMatching = false;
        this->matchRoomIdIsNotDefault = false;
        this->matchState = MatchState::Idle;
    }

    void completeMatchmaking()
    {
        if (!this->matchCallback) {
            this->matchState = MatchState::Idle;
            return;
        }
        if (this->matchCompleting) {
            return;
        }
        this->matchCompleting = true;
        struct ResetCompleting {
            CSteam* self;
            ~ResetCompleting()
            {
                if (self) {
                    self->matchCompleting = false;
                }
            }
        } reset{this};
        auto* networking = SteamNetworkingSockets();
        if (!networking || this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
            this->failMatchmaking("Connection unavailable");
            return;
        }
        SteamNetConnectionInfo_t connInfo{};
        if (!networking->GetConnectionInfo(this->matchInfo.connection, &connInfo) ||
            connInfo.m_eState != k_ESteamNetworkingConnectionState_Connected) {
            this->failMatchmaking("Connection not ready");
            return;
        }

        const std::function<bool(const std::string&)> validateVR =
            [](const std::string& payload) -> bool { return payload == PROTOCOL_VERSION; };

        const int handshakeTimeoutMs = 50000;
        if (this->matchInfo.isServer) {
            if (!this->p2pReceive(handshakeTimeoutMs, validateVR) || !this->p2pSend(PROTOCOL_VERSION)) {
                this->failMatchmaking("Protocol handshake failed (server)");
                return;
            }
        } else {
            if (!this->p2pSend(PROTOCOL_VERSION) || !this->p2pReceive(handshakeTimeoutMs, validateVR)) {
                this->failMatchmaking("Protocol handshake failed (client)");
                return;
            }
        }

        this->matchState = MatchState::Completed;
        auto cb = this->matchCallback;
        auto matchInfoCopy = this->matchInfo;
        this->matchCallback = nullptr;
        this->matchTimeoutSec = 0;
        this->matchOwnsSockets = false;
        this->matchState = MatchState::Idle;
        cb(matchInfoCopy);
    }

    void onLobbyDataUpdated(LobbyDataUpdate_t* data)
    {
        if (!this->lobbyDataRequestState.active || !data) {
            return;
        }
        if (data->m_ulSteamIDLobby != this->lobbyDataRequestState.lobbyID.ConvertToUint64()) {
            return;
        }
        this->lobbyDataRequestState.completed = true;
        this->lobbyDataRequestState.success = data->m_bSuccess != 0;
    }

    void onLobbyJoinList(LobbyMatchList_t* list, bool bIOFailure)
    {
        if (this->matchState != MatchState::Searching) {
            return;
        }
        if (bIOFailure || !list) {
            this->failMatchmaking("Lobby list I/O failure");
            return;
        }
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            this->failMatchmaking("SteamMatchmaking unavailable");
            return;
        }
        if (0 < list->m_nLobbiesMatching) {
            CSteamID lobbyID = matchmaking->GetLobbyByIndex(0);
            this->matchInfo.lobbyID = lobbyID;
            SteamAPICall_t call = matchmaking->JoinLobby(lobbyID);
            if (call == k_uAPICallInvalid) {
                this->failMatchmaking("JoinLobby failed");
                return;
            }
            this->matchState = MatchState::Joining;
            this->callbackLobbyEnter.Set(call, this, &CSteam::onLobbyEnter);
            putlog("joinLobbyWithRoomId: joining lobby %llu", lobbyID.ConvertToUint64());
            return;
        } else {
            // 目的のロビーが見つからなければ即切断
            putlog("joinLobbyWithRoomId: room not found.");
            this->failMatchmaking("Room not found");
        }
    }

    void onLobbyMatchList(LobbyMatchList_t* list, bool bIOFailure)
    {
        if (this->matchState != MatchState::Searching) {
            return;
        }
        if (bIOFailure || !list) {
            this->failMatchmaking("Lobby match list I/O failure");
            return;
        }
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            this->failMatchmaking("SteamMatchmaking unavailable");
            return;
        }
        CSteamID lobbyID;
        if (this->findUsableLobby(list, this->matchGame, this->matchRound, this->matchLobbyType, this->matchRoomId, &lobbyID)) {
            if (this->matchRequestFromStartMatching && this->matchRoomIdIsNotDefault) {
                putlog("startMatching: duplicate room id.");
                this->failMatchmaking("Duplicate room ID");
                return;
            }
            this->matchInfo.lobbyID = lobbyID;
            SteamAPICall_t call = matchmaking->JoinLobby(lobbyID);
            if (call == k_uAPICallInvalid) {
                this->failMatchmaking("JoinLobby failed");
                return;
            }
            this->matchState = MatchState::Joining;
            this->callbackLobbyEnter.Set(call, this, &CSteam::onLobbyEnter);
            putlog("startMatching: joining lobby %llu", lobbyID.ConvertToUint64());
            return;
        }
        SteamAPICall_t call = matchmaking->CreateLobby(this->matchLobbyType, 2);
        if (call == k_uAPICallInvalid) {
            this->failMatchmaking("CreateLobby failed");
            return;
        }
        this->matchState = MatchState::Creating;
        this->callbackLobbyCreated.Set(call, this, &CSteam::onLobbyCreated);
        putlog("startMatching: creating new lobby");
    }

    void onLobbyAvailabilityCheck(LobbyAvailabilityCheckKind kind, LobbyMatchList_t* list, bool bIOFailure)
    {
        auto& state = this->lobbyAvailabilityCheckState(kind);
        if (!state.active) {
            return;
        }

        state.active = false;
        if (bIOFailure || !list) {
            putlog("startLobbyAvailabilityCheck[%d]: lobby list I/O failure.", (int)kind);
            state.delayingMiss = false;
            state.result = 0;
            return;
        }

        if (this->findUsableLobby(list, state.game, state.round, state.lobbyType, state.roomId, nullptr)) {
            state.delayingMiss = false;
            state.result = 1;
        } else {
            state.delayingMiss = true;
            state.result = 255;
            state.missDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        }
        putlog("startLobbyAvailabilityCheck[%d]: result=%u (matched=%u)",
               (int)kind,
               state.result,
               list->m_nLobbiesMatching);
    }

    void onQuickLobbyAvailabilityCheck(LobbyMatchList_t* list, bool bIOFailure)
    {
        this->onLobbyAvailabilityCheck(LobbyAvailabilityCheckKind::QuickMatch, list, bIOFailure);
    }

    void onFriendsLobbyAvailabilityCheck(LobbyMatchList_t* list, bool bIOFailure)
    {
        this->onLobbyAvailabilityCheck(LobbyAvailabilityCheckKind::FriendsMatch, list, bIOFailure);
    }

    void onLobbyCreated(LobbyCreated_t* created, bool bIOFailure)
    {
        if (this->matchState != MatchState::Creating) {
            return;
        }
        if (bIOFailure || !created || created->m_eResult != k_EResultOK) {
            putlog("Lobby creation failed: result=%d", created ? created->m_eResult : -1);
            this->failMatchmaking("Lobby creation failed");
            return;
        }
        this->matchInfo.lobbyID = CSteamID(created->m_ulSteamIDLobby);
        auto* matchmaking = SteamMatchmaking();
        if (!matchmaking) {
            this->failMatchmaking("SteamMatchmaking unavailable");
            return;
        }
        matchmaking->SetLobbyJoinable(this->matchInfo.lobbyID, true);
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "game_type", this->gameTypeToString(this->matchGame).c_str());
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "round_type", this->roundTypeToString(this->matchRound).c_str());
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "room_id", this->matchRoomId.c_str());
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "lobby_type", this->lobbyTypeToString(this->matchLobbyType).c_str());
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "joinable", "1");
        putlog("startMatching: lobby %llu created", this->matchInfo.lobbyID.ConvertToUint64());

        // --- P2P サーバ側のセットアップ ---
        auto* networking = SteamNetworkingSockets();
        if (!networking) {
            this->failMatchmaking("SteamNetworkingSockets unavailable");
            return;
        }
        SteamNetworkingIdentity nid;
        if (!networking->GetIdentity(&nid)) {
            this->failMatchmaking("Failed to get network identity");
            return;
        }
        char identityBuf[256];
        nid.ToString(identityBuf, sizeof(identityBuf));
        putlog("Network Identity: %s", identityBuf);

        this->matchInfo.isServer = true;
        if (this->matchInfo.listenPort == 0) {
            this->matchInfo.listenPort = this->chooseVirtualPort();
        }
        this->matchInfo.serverIdentity.Clear();
        this->matchInfo.serverIdentity.SetSteamID(SteamUser()->GetSteamID());

        this->matchInfo.listenSocket = networking->CreateListenSocketP2P(this->matchInfo.listenPort, 0, nullptr);
        if (this->matchInfo.listenSocket == k_HSteamListenSocket_Invalid) {
            this->failMatchmaking("CreateListenSocketP2P failed");
            return;
        }
        this->matchOwnsSockets = true;

        this->matchInfo.serverIdentity.ToString(identityBuf, sizeof(identityBuf));
        char portBuf[16];
        snprintf(portBuf, sizeof(portBuf), "%u", this->matchInfo.listenPort);
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "p2p_server_addr", identityBuf);
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "p2p_server_port", portBuf);
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "signaling", "steam");
        matchmaking->SetLobbyData(this->matchInfo.lobbyID, "joinable", "2"); // server ready

        this->matchState = MatchState::WaitingForClient;
        putlog("Waiting for client... (identity=%s, port=%s)", identityBuf, portBuf);
    }

    void onLobbyEnter(LobbyEnter_t* entered, bool bIOFailure)
    {
        if (this->matchState != MatchState::Joining) {
            return;
        }
        if (bIOFailure || !entered || entered->m_EChatRoomEnterResponse != k_EChatRoomEnterResponseSuccess) {
            this->failMatchmaking("Failed to enter lobby");
            return;
        }
        this->matchInfo.lobbyID = CSteamID(entered->m_ulSteamIDLobby);
        auto* matchmaking = SteamMatchmaking();
        auto* networking = SteamNetworkingSockets();
        if (!matchmaking || !networking) {
            this->failMatchmaking("Steam interfaces unavailable");
            return;
        }
        this->matchState = MatchState::LobbyEntered;
        const char* portKey = "p2p_server_port";
        const char* addrKey = "p2p_server_addr";
        CSteamID owner = matchmaking->GetLobbyOwner(this->matchInfo.lobbyID);
        CSteamID me = SteamUser()->GetSteamID();
        if (owner == me) {
            // ホストとして入室するパスは onLobbyCreated で処理済みなのでここでは何もしない
            putlog("onLobbyEnter: owner == me (host), ignoring client path");
            return;
        }

        this->matchInfo.isServer = false;
        const char* encodedIdentity = matchmaking->GetLobbyData(this->matchInfo.lobbyID, addrKey);
        const char* portValue = matchmaking->GetLobbyData(this->matchInfo.lobbyID, portKey);
        if (!encodedIdentity || !encodedIdentity[0] || !portValue || !portValue[0]) {
            this->failMatchmaking("Missing P2P bootstrap metadata");
            return;
        }
        SteamNetworkingIdentity serverIdentity;
        if (!serverIdentity.ParseString(encodedIdentity)) {
            this->failMatchmaking("Invalid server identity");
            return;
        }
        uint16 listenPort = static_cast<uint16>(atoi(portValue));
        this->matchInfo.serverIdentity = serverIdentity;
        this->matchInfo.listenPort = listenPort;

        char identityBuf[256];
        this->matchInfo.serverIdentity.ToString(identityBuf, sizeof(identityBuf));
        putlog("startMatching: connecting to server... (identity=%s, port=%s)", identityBuf, portValue);

        HSteamNetConnection connection = networking->ConnectP2P(serverIdentity, listenPort, 0, nullptr);
        if (connection == k_HSteamNetConnection_Invalid) {
            this->failMatchmaking("ConnectP2P failed");
            return;
        }

        SteamNetConnectionInfo_t info;
        networking->GetConnectionInfo(connection, &info);
        putlog("client: ConnectP2P created conn=%d, state=%d", (int)connection, (int)info.m_eState);

        if (this->matchInfo.listenSocket != k_HSteamListenSocket_Invalid) {
            putlog("warning: close listen socket");
            networking->CloseListenSocket(this->matchInfo.listenSocket);
            this->matchInfo.listenSocket = k_HSteamListenSocket_Invalid;
        }
        this->matchInfo.connection = connection;
        this->clearReceiveQueue();
        this->matchState = MatchState::Connecting;
    }

    void deactivate()
    {
        this->actUp = 0;
        this->actDown = 0;
        this->actLeft = 0;
        this->actRight = 0;
        this->actA = 0;
        this->actB = 0;
        this->actX = 0;
        this->actY = 0;
        this->actStart = 0;
    }

    inline void clearButtonState()
    {
        this->buttonState.up = false;
        this->buttonState.down = false;
        this->buttonState.left = false;
        this->buttonState.right = false;
        this->buttonState.a = false;
        this->buttonState.b = false;
        this->buttonState.x = false;
        this->buttonState.y = false;
        this->buttonState.start = false;
    }

    void updateButtonState()
    {
        auto previousType = buttonState.type;
        this->clearButtonState();
        if (!initialized) {
            putlog("warning: called updateButtonState without initialization");
            buttonState.type = ControllerType::NotConnected;
            return;
        }
        SteamInput()->RunFrame();
        InputHandle_t inputHandles[STEAM_INPUT_MAX_COUNT];
        int num = SteamInput()->GetConnectedControllers(inputHandles);
        buttonState.connected = 0 < num;
        if (!buttonState.connected) {
            buttonState.type = ControllerType::NotConnected;
            return;
        }

        // 接続コントローラの種別をチェック
        InputHandle_t inputHandle = 0;
        for (int i = 0; 0 == inputHandle && i < num; i++) {
            switch (SteamInput()->GetInputTypeForHandle(inputHandles[i])) {
                case k_ESteamInputType_SteamController:
                case k_ESteamInputType_XBox360Controller:
                case k_ESteamInputType_XBoxOneController:
                case k_ESteamInputType_SteamDeckController:
                    inputHandle = inputHandles[i];
                    buttonState.type = ControllerType::XBOX;
                    break;
                case k_ESteamInputType_SwitchJoyConPair:
                case k_ESteamInputType_SwitchProController:
                    inputHandle = inputHandles[i];
                    buttonState.type = ControllerType::NintendoSwitch;
                    break;
                case k_ESteamInputType_PS3Controller:
                case k_ESteamInputType_PS4Controller:
                case k_ESteamInputType_PS5Controller:
                    inputHandle = inputHandles[i];
                    buttonState.type = ControllerType::PlayStation;
                    break;
                default:
                    break;
            }
        }
        if (!inputHandle) {
            // 対応コントローラが接続されていないので未接続とする
            buttonState.type = ControllerType::NotConnected;
            return;
        }

        // 直前フレームからコントローラの種類が変わった場合は一度ディアクティブ
        if (previousType != buttonState.type) {
            this->deactivate();
        }
        if (!this->activate(inputHandle)) {
            buttonState.type = ControllerType::NotConnected;
            return;
        }

        // 現在の入力状態を取得して buttonState を更新
        auto move = SteamInput()->GetAnalogActionData(inputHandle, actMove);
        auto up = SteamInput()->GetDigitalActionData(inputHandle, actUp);
        auto down = SteamInput()->GetDigitalActionData(inputHandle, actDown);
        auto left = SteamInput()->GetDigitalActionData(inputHandle, actLeft);
        auto right = SteamInput()->GetDigitalActionData(inputHandle, actRight);
        auto a = SteamInput()->GetDigitalActionData(inputHandle, actA);
        auto b = SteamInput()->GetDigitalActionData(inputHandle, actB);
        auto x = SteamInput()->GetDigitalActionData(inputHandle, actX);
        auto y = SteamInput()->GetDigitalActionData(inputHandle, actY);
        auto start = SteamInput()->GetDigitalActionData(inputHandle, actStart);

        // アナログスティックの情報を設定
        const float deadzone = 0.2f;
        float mx = move.x;
        float my = move.y;
        float magnitude = std::sqrt(mx * mx + my * my);
        if (magnitude < deadzone) {
            ;
        } else {
            mx /= magnitude;
            my /= magnitude;
            const float diagonalThreshold = 0.382683f;
            if (my > diagonalThreshold) {
                if (mx > diagonalThreshold) {
                    this->buttonState.right = true;
                } else if (mx < -diagonalThreshold) {
                    this->buttonState.left = true;
                }
                this->buttonState.up = true;
            } else if (my < -diagonalThreshold) {
                if (mx > diagonalThreshold) {
                    this->buttonState.right = true;
                } else if (mx < -diagonalThreshold) {
                    this->buttonState.left = true;
                }
                this->buttonState.down = true;
            } else {
                if (mx > diagonalThreshold) {
                    this->buttonState.right = true;
                } else if (mx < -diagonalThreshold) {
                    this->buttonState.left = true;
                }
            }
        }

        // デジタルボタンの情報を設定
        this->buttonState.up |= up.bState;
        this->buttonState.down |= down.bState;
        this->buttonState.left |= left.bState;
        this->buttonState.right |= right.bState;
        this->buttonState.a = a.bState;
        this->buttonState.b = b.bState;
        this->buttonState.x = x.bState;
        this->buttonState.y = y.bState;
        this->buttonState.start = start.bState;
    }

    bool activate(InputHandle_t)
    {
        if (!this->actMove) {
            this->actMove = SteamInput()->GetAnalogActionHandle("Move");
            if (!this->actMove) {
                return false;
            }
        }
        if (!this->actUp) {
            this->actUp = SteamInput()->GetDigitalActionHandle("dpad_up");
            if (!this->actUp) {
                return false;
            }
        }
        if (!this->actDown) {
            this->actDown = SteamInput()->GetDigitalActionHandle("dpad_down");
            if (!this->actDown) {
                return false;
            }
        }
        if (!this->actLeft) {
            this->actLeft = SteamInput()->GetDigitalActionHandle("dpad_left");
            if (!this->actLeft) {
                return false;
            }
        }
        if (!this->actRight) {
            this->actRight = SteamInput()->GetDigitalActionHandle("dpad_right");
            if (!this->actRight) {
                return false;
            }
        }
        if (!this->actA) {
            this->actA = SteamInput()->GetDigitalActionHandle("button_a");
            if (!this->actA) {
                return false;
            }
        }
        if (!this->actB) {
            this->actB = SteamInput()->GetDigitalActionHandle("button_b");
            if (!this->actB) {
                return false;
            }
        }
        if (!this->actX) {
            this->actX = SteamInput()->GetDigitalActionHandle("button_x");
            if (!this->actX) {
                return false;
            }
        }
        if (!this->actY) {
            this->actY = SteamInput()->GetDigitalActionHandle("button_y");
            if (!this->actY) {
                return false;
            }
        }
        if (!this->actStart) {
            this->actStart = SteamInput()->GetDigitalActionHandle("button_start");
            if (!this->actStart) {
                return false;
            }
        }
        return true;
    }

    // NetworkingSockets のコールバック本体
    void onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* status);
};

// SteamNetworkingSockets の connection status 変更コールバック
inline void CSteam::onConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* status)
{
    if (!status) {
        putlog("onConnectionStatusChanged (no status)");
        return;
    }

    putlog("onConnectionStatusChanged: state=%d, listen=%d, conn=%d",
           (int)status->m_info.m_eState,
           (int)status->m_info.m_hListenSocket,
           (int)status->m_hConn);

    if (status->m_info.m_eState == k_ESteamNetworkingConnectionState_Connecting &&
        this->matchState == MatchState::WaitingForClient &&
        status->m_info.m_hListenSocket == this->matchInfo.listenSocket) {
        auto* networking = SteamNetworkingSockets();
        if (!networking) {
            this->failMatchmaking("SteamNetworkingSockets unavailable");
            return;
        }
        putlog("P2P: incoming connection request, accepting...");
        EResult result = networking->AcceptConnection(status->m_hConn);
        if (result != k_EResultOK) {
            this->failMatchmaking("AcceptConnection failed");
            return;
        }
        this->matchInfo.connection = status->m_hConn;
        this->clearReceiveQueue();
        this->matchState = MatchState::Connecting;
        return;
    }

    if (status->m_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
        const bool isTargetConnection = (status->m_hConn == this->matchInfo.connection) ||
                                        (status->m_info.m_hListenSocket == this->matchInfo.listenSocket);
        if (isTargetConnection && this->matchCallback) {
            if (this->matchInfo.connection == k_HSteamNetConnection_Invalid) {
                this->matchInfo.connection = status->m_hConn;
            }
            this->clearReceiveQueue();
            putlog("P2P: connection established, completing matchmaking");
            this->completeMatchmaking();
        }
        return;
    }

    // それ以外（クライアント側の状態変化など）は、必要になったらここで拡張
}

// SteamUI のオーバーレイ状態が変化したことを捕捉するコールバック
inline void CSteam::onGameOverlayActivated(GameOverlayActivated_t* args)
{
    putlog("Overlay changed: %s", args->m_bActive ? "true" : "false");
    this->overlay = args->m_bActive;
}
