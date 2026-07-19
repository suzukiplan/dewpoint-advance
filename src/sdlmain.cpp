/**
 * Dewpoint Advance Runtime (SDL2)
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
#include "dewpoint_runtime.h"
#include "dewpoint_define.h"
#include "mgbahelper.h"
#include "pathutil.h"
#include "steam.hpp"

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <mgba/core/log.h>
#include <SDL.h>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

extern "C" {
extern const uint8_t game_rom[];
extern const size_t game_rom_size;
}

namespace
{
constexpr int WINDOW_SCALE = 3;
constexpr int AUDIO_FREQUENCY = 44100;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_SAMPLES = 2048;
constexpr Uint32 TARGET_QUEUED_AUDIO_SIZE = AUDIO_FREQUENCY * AUDIO_CHANNELS * sizeof(int16_t) / 20;

struct WindowConfig {
    int32_t fullscreen;
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;
};

static_assert(sizeof(WindowConfig) == 20, "WindowConfig must use five 4-byte fields");

class ScopedLogger
{
  private:
    mStandardLogger logger;

  public:
    ScopedLogger()
        : logger{}
    {
        mStandardLoggerInit(&logger);
        logger.logToStdout = true;
        logger.d.filter->defaultLevels = mLOG_FATAL | mLOG_ERROR | mLOG_WARN | mLOG_GAME_ERROR;
        mLogSetDefaultLogger(&logger.d);
    }

    ~ScopedLogger()
    {
        mLogSetDefaultLogger(nullptr);
        mStandardLoggerDeinit(&logger);
    }
};

class ScopedSdl
{
  private:
    bool initialized;

  public:
    ScopedSdl()
        : initialized(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) == 0)
    {
        if (initialized) {
            // SDL2 enables text input on desktop by default. This runtime only
            // handles game controls, so keep macOS from opening its accent menu
            // when a letter key is held down.
            SDL_StopTextInput();
        }
    }

    ~ScopedSdl()
    {
        if (initialized) {
            SDL_Quit();
        }
    }

    explicit operator bool() const { return initialized; }
};

void updateKeyState(mGBAHelper::KeyState* state, SDL_Keycode key, bool pressed)
{
    switch (key) {
        case SDLK_UP: state->up = pressed; break;
        case SDLK_DOWN: state->down = pressed; break;
        case SDLK_LEFT: state->left = pressed; break;
        case SDLK_RIGHT: state->right = pressed; break;
        case SDLK_z: state->b = pressed; break;
        case SDLK_x: state->a = pressed; break;
        case SDLK_a: state->l = pressed; break;
        case SDLK_s: state->r = pressed; break;
        case SDLK_SPACE: state->start = pressed; break;
        case SDLK_ESCAPE: state->select = pressed; break;
    }
}

void updateGbaKeyState(
    mGBAHelper::KeyState* state,
    const mGBAHelper::KeyState& keyboardState,
    const CSteam::ButtonState& steamState)
{
    state->up = keyboardState.up || steamState.up;
    state->down = keyboardState.down || steamState.down;
    state->left = keyboardState.left || steamState.left;
    state->right = keyboardState.right || steamState.right;
    state->a = keyboardState.a || steamState.a;
    state->b = keyboardState.b || steamState.b;
    state->l = keyboardState.l || steamState.l;
    state->r = keyboardState.r || steamState.r;
    state->start = keyboardState.start || steamState.start;
    state->select = keyboardState.select || steamState.select;
}

bool readFile(const char* path, std::vector<uint8_t>* data)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }

    const std::streamsize size = input.tellg();
    if (size <= 0 || static_cast<uintmax_t>(size) > std::numeric_limits<size_t>::max()) {
        return false;
    }
    data->resize(static_cast<size_t>(size));
    input.seekg(0);
    return static_cast<bool>(input.read(reinterpret_cast<char*>(data->data()), size));
}

WindowConfig defaultWindowConfig()
{
    return WindowConfig{
        -1,
        GBA_VRAM_WIDTH * WINDOW_SCALE,
        GBA_VRAM_HEIGHT * WINDOW_SCALE,
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
    };
}

WindowConfig loadWindowConfig(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        bool exists = false;
        bool isDirectory = false;
        if (!DewpointPath::inspect(path, &exists, &isDirectory, nullptr) || exists) {
            std::cerr << "Failed to read window configuration: " << path << '\n';
        }
        return defaultWindowConfig();
    }

    WindowConfig config{};
    if (input.tellg() != static_cast<std::streamsize>(sizeof(config))) {
        std::cerr << "Invalid window configuration size: " << path << '\n';
        return defaultWindowConfig();
    }
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(&config), sizeof(config)) ||
        (config.fullscreen != -1 && config.fullscreen != 0) || config.width <= 0 || config.height <= 0) {
        std::cerr << "Invalid window configuration: " << path << '\n';
        return defaultWindowConfig();
    }
    return config;
}

bool saveWindowConfig(
    const std::string& path,
    SDL_Window* window,
    int windowedWidth,
    int windowedHeight,
    int windowedX,
    int windowedY)
{
    const bool fullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
    if (!fullscreen) {
        SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
        SDL_GetWindowPosition(window, &windowedX, &windowedY);
    }

    const WindowConfig config{
        fullscreen ? -1 : 0,
        windowedWidth,
        windowedHeight,
        windowedX,
        windowedY,
    };
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(reinterpret_cast<const char*>(&config), sizeof(config));
    output.flush();
    return static_cast<bool>(output);
}

bool isFullscreen(SDL_Window* window)
{
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
}

void updateCursorVisibility(SDL_Window* window)
{
    const int toggle = isFullscreen(window) ? SDL_DISABLE : SDL_ENABLE;
    if (SDL_ShowCursor(toggle) < 0) {
        std::cerr << "SDL_ShowCursor failed: " << SDL_GetError() << '\n';
    }
}

void printUsage(const char* executable)
{
    std::cerr << "Usage: " << executable << " [-s <save.dat>] [-c <config.dat>] [rom.gba]\n";
}

bool getApplicationInstallDirectory(std::string* installDirectory)
{
    char* basePath = SDL_GetBasePath();
    if (!basePath) {
        std::cerr << "Failed to get application installation directory: " << SDL_GetError() << '\n';
        return false;
    }

    *installDirectory = basePath;
    SDL_free(basePath);
    return true;
}

bool getSteamInstallDirectory(std::string* installDirectory)
{
    auto* apps = SteamApps();
    auto* utils = SteamUtils();
    if (!apps || !utils) {
        std::cerr << "Failed to access Steam installation information\n";
        return false;
    }

    std::vector<char> pathBuffer(4096);
    if (apps->GetAppInstallDir(utils->GetAppID(), pathBuffer.data(), pathBuffer.size()) == 0) {
        std::cerr << "Failed to get Steam App installation directory\n";
        return false;
    }

    *installDirectory = pathBuffer.data();
    return true;
}

bool redirectLogsToFile(const std::string& path)
{
    std::cout.flush();
    std::cerr.flush();

#ifdef _WIN32
    FILE* logFile = std::fopen(path.c_str(), "w");
    const auto redirect = [](FILE* source, FILE* destination) {
        return _dup2(_fileno(source), _fileno(destination)) == 0;
    };
#else
    FILE* logFile = std::fopen(path.c_str(), "w");
    const auto redirect = [](FILE* source, FILE* destination) {
        return dup2(fileno(source), fileno(destination)) >= 0;
    };
#endif
    if (!logFile) {
        return false;
    }

    const bool redirected = redirect(logFile, stdout) && redirect(logFile, stderr);
    std::fclose(logFile);
    return redirected;
}

bool configureSteamSavePaths(
    const std::string& installDirectory,
    bool usesDefaultSramPath,
    bool usesDefaultConfigPath,
    std::string* sramPath,
    std::string* configPath)
{
    const std::string saveDirectory = DewpointPath::join(installDirectory, "save");
    std::string errorMessage;
    if (!DewpointPath::createDirectory(saveDirectory, &errorMessage)) {
        std::cerr << "Failed to create default save directory: " << saveDirectory << ": "
                  << errorMessage << '\n';
        return false;
    }

    if (usesDefaultSramPath) {
        *sramPath = DewpointPath::join(saveDirectory, "save.dat");
    }
    if (usesDefaultConfigPath) {
        *configPath = DewpointPath::join(saveDirectory, "config.dat");
    }
    return true;
}
} // namespace

int main(int argc, char* argv[])
{
    ScopedLogger logger;

    std::string romPath;
    std::string sramPath = "save.dat";
    std::string configPath = "config.dat";
    bool usesDefaultSramPath = true;
    bool usesDefaultConfigPath = true;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-s" || argument == "-c") {
            if (++i >= argc) {
                printUsage(argv[0]);
                return 1;
            }
            if (argument == "-s") {
                sramPath = argv[i];
                usesDefaultSramPath = false;
            } else {
                configPath = argv[i];
                usesDefaultConfigPath = false;
            }
        } else if (!romPath.empty()) {
            printUsage(argv[0]);
            return 1;
        } else {
            romPath = argument;
        }
    }

    std::string logInstallDirectory;
    bool logsRedirected = false;
    if (SteamAPI_IsSteamRunning()) {
        if (!getApplicationInstallDirectory(&logInstallDirectory)) {
            return 1;
        }
        const std::string logPath = DewpointPath::join(logInstallDirectory, "log.txt");
        if (!redirectLogsToFile(logPath)) {
            std::cerr << "Failed to redirect logs to Steam log file: " << logPath << '\n';
            return 1;
        }
        logsRedirected = true;
    }

    ScopedSdl sdl;
    if (!sdl) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    mGBAHelper gba;
    DewpointRuntime dewpoint(gba, [](const char* message) {
        std::cerr << "[Steam] " << message << '\n';
    });
    char* preferencePath = SDL_GetPrefPath("SUZUKI PLAN", APP_NAME);
    if (!preferencePath) {
        std::cerr << "Failed to get the local application data directory: " << SDL_GetError() << '\n';
    } else {
        const std::string highScoreDirectory = DewpointPath::join(preferencePath, "leaderboard-cache");
        SDL_free(preferencePath);
        std::string errorMessage;
        if (!DewpointPath::createDirectory(highScoreDirectory, &errorMessage) ||
            !dewpoint.setHighScoreStorageDirectory(highScoreDirectory)) {
            std::cerr << "Failed to prepare the pending high score directory: "
                      << highScoreDirectory << ": " << errorMessage << '\n';
        }
    }
    const bool steamInitialized = dewpoint.initialize();
    if (steamInitialized) {
        std::string installDirectory;
        if (!getSteamInstallDirectory(&installDirectory)) {
            return 1;
        }
        if (!logsRedirected || !DewpointPath::same(logInstallDirectory, installDirectory)) {
            const std::string logPath = DewpointPath::join(installDirectory, "log.txt");
            if (!redirectLogsToFile(logPath)) {
                std::cerr << "Failed to redirect logs to Steam log file: " << logPath << '\n';
                return 1;
            }
        }
        if ((usesDefaultSramPath || usesDefaultConfigPath) &&
            !configureSteamSavePaths(
                installDirectory,
                usesDefaultSramPath,
                usesDefaultConfigPath,
                &sramPath,
                &configPath)) {
            return 1;
        }
    }

    std::vector<uint8_t> rom;
    const uint8_t* romData = game_rom;
    size_t romSize = game_rom_size;
    if (!romPath.empty() && !readFile(romPath.c_str(), &rom)) {
        std::cerr << "Failed to read GBA ROM: " << romPath << '\n';
        return 1;
    }
    if (!romPath.empty()) {
        romData = rom.data();
        romSize = rom.size();
    }

    gba.setSramPath(sramPath);
    if (!gba.load(romData, romSize)) {
        if (romPath.empty()) {
            std::cerr << "Failed to load embedded GBA ROM\n";
        } else {
            std::cerr << "Failed to load GBA ROM: " << romPath << '\n';
        }
        return 1;
    }

    CSteam steamInput;
    steamInput.setLoggger([](const char* message) {
        std::cerr << "[SteamInput] " << message << '\n';
    });
    const bool steamInputInitialized = steamInitialized && steamInput.initializeInput();

    const WindowConfig config = loadWindowConfig(configPath);
    const bool windowModeEnabled = CSteam::isEnabledWindowModo();
    int windowedWidth = config.width;
    int windowedHeight = config.height;
    int windowedX = config.x;
    int windowedY = config.y;

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window* window = SDL_CreateWindow(
        APP_NAME,
        windowedX,
        windowedY,
        windowedWidth,
        windowedHeight,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
    SDL_GetWindowPosition(window, &windowedX, &windowedY);
    if ((!windowModeEnabled || config.fullscreen == -1) &&
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP) != 0) {
        std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
    }
    updateCursorVisibility(window);
    dewpoint.setFullscreenCallbacks(
        [window, windowModeEnabled, &windowedWidth, &windowedHeight, &windowedX, &windowedY](bool fullscreen) {
            const bool wasFullscreen = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
            if (!fullscreen && !windowModeEnabled) {
                return wasFullscreen;
            }
            if (fullscreen && !wasFullscreen) {
                SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
                SDL_GetWindowPosition(window, &windowedX, &windowedY);
            }
            const Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
            if (SDL_SetWindowFullscreen(window, flags) != 0) {
                std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
            }
            const bool fullscreenEnabled = isFullscreen(window);
            updateCursorVisibility(window);
            if (!fullscreenEnabled) {
                SDL_GetWindowSize(window, &windowedWidth, &windowedHeight);
                SDL_GetWindowPosition(window, &windowedX, &windowedY);
            }
            return fullscreenEnabled;
        },
        [window]() {
            return isFullscreen(window);
        });
    const auto saveConfig = [&]() {
        if (!saveWindowConfig(configPath, window, windowedWidth, windowedHeight, windowedX, windowedY)) {
            std::cerr << "Failed to save window configuration: " << configPath << '\n';
        }
    };

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        saveConfig();
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, gba.getVramWidth(), gba.getVramHeight());

    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_ABGR8888,
        SDL_TEXTUREACCESS_STREAMING,
        gba.getVramWidth(),
        gba.getVramHeight());
    if (!texture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        saveConfig();
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return 1;
    }

    SDL_AudioSpec desired{};
    desired.freq = AUDIO_FREQUENCY;
    desired.format = AUDIO_S16SYS;
    desired.channels = AUDIO_CHANNELS;
    desired.samples = AUDIO_SAMPLES;
    SDL_AudioDeviceID audioDevice = SDL_OpenAudioDevice(nullptr, 0, &desired, nullptr, 0);
    if (!audioDevice) {
        std::cerr << "SDL_OpenAudioDevice failed: " << SDL_GetError() << '\n';
        saveConfig();
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_PauseAudioDevice(audioDevice, 0);

    bool running = true;
    bool paused = false;
    int exitCode = 0;
    mGBAHelper::KeyState keyboardState{};
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT) {
                updateCursorVisibility(window);
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    keyboardState = {};
                } else if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) == 0) {
                    if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                        windowedWidth = event.window.data1;
                        windowedHeight = event.window.data2;
                    } else if (event.window.event == SDL_WINDOWEVENT_MOVED) {
                        windowedX = event.window.data1;
                        windowedY = event.window.data2;
                    }
                }
            } else if (event.type == SDL_KEYDOWN) {
                const bool command = (event.key.keysym.mod & KMOD_GUI) != 0;
                if (command && event.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (command && event.key.keysym.sym == SDLK_r && !event.key.repeat) {
                    gba.reset();
                    SDL_ClearQueuedAudio(audioDevice);
                } else if (command && event.key.keysym.sym == SDLK_p && !event.key.repeat) {
                    paused = !paused;
                    SDL_ClearQueuedAudio(audioDevice);
                    SDL_PauseAudioDevice(audioDevice, paused ? 1 : 0);
                } else if (!command) {
                    updateKeyState(&keyboardState, event.key.keysym.sym, true);
                }
            } else if (event.type == SDL_KEYUP) {
                updateKeyState(&keyboardState, event.key.keysym.sym, false);
            }
        }
        if (!running) {
            break;
        }

        dewpoint.tick();
        if (steamInputInitialized) {
            steamInput.updateInput();
        }
        switch (steamInput.buttonState.type) {
            case CSteam::ControllerType::XBOX:
            case CSteam::ControllerType::NintendoSwitch:
                dewpoint.setButtonInputType(DewpointRuntime::ButtonInputType::XboxOrSwitch);
                break;
            case CSteam::ControllerType::PlayStation:
                dewpoint.setButtonInputType(DewpointRuntime::ButtonInputType::PlayStation);
                break;
            case CSteam::ControllerType::NotConnected:
                dewpoint.setButtonInputType(DewpointRuntime::ButtonInputType::PCKeyboard);
                break;
        }
        updateGbaKeyState(&gba.keyState, keyboardState, steamInput.buttonState);
        if (dewpoint.takeExitRequest(&exitCode)) {
            running = false;
            break;
        }
        if (paused) {
            SDL_Delay(10);
            continue;
        }
        gba.tick();

        size_t soundSize = 0;
        uint16_t* sound = gba.dequeSound(&soundSize);
        if (sound && soundSize) {
            if (SDL_QueueAudio(audioDevice, sound, static_cast<Uint32>(soundSize)) != 0) {
                std::cerr << "SDL_QueueAudio failed: " << SDL_GetError() << '\n';
                running = false;
            }
        }

        if (SDL_UpdateTexture(
                texture,
                nullptr,
                gba.getVram(),
                gba.getVramWidth() * static_cast<int>(sizeof(uint32_t))) != 0) {
            std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
            running = false;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        while (SDL_GetAudioDeviceStatus(audioDevice) == SDL_AUDIO_PLAYING &&
               SDL_GetQueuedAudioSize(audioDevice) > TARGET_QUEUED_AUDIO_SIZE) {
            SDL_Delay(1);
        }
    }

    SDL_PauseAudioDevice(audioDevice, 1);
    SDL_CloseAudioDevice(audioDevice);
    saveConfig();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return exitCode;
}
