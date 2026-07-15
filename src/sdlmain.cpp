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
#include "mgbahelper.h"

#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <mgba/core/log.h>
#include <SDL.h>

namespace
{
constexpr int WINDOW_SCALE = 3;
constexpr int AUDIO_FREQUENCY = 44100;
constexpr int AUDIO_CHANNELS = 2;
constexpr int AUDIO_SAMPLES = 2048;
constexpr Uint32 TARGET_QUEUED_AUDIO_SIZE = AUDIO_FREQUENCY * AUDIO_CHANNELS * sizeof(int16_t) / 20;

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
} // namespace

int main(int argc, char* argv[])
{
    ScopedLogger logger;

    std::string romPath;
    std::string sramPath = "save.dat";
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "-s") {
            if (++i >= argc) {
                std::cerr << "Usage: " << argv[0] << " [-s <save.dat>] <rom.gba>\n";
                return 1;
            }
            sramPath = argv[i];
        } else if (!romPath.empty()) {
            std::cerr << "Usage: " << argv[0] << " [-s <save.dat>] <rom.gba>\n";
            return 1;
        } else {
            romPath = argument;
        }
    }
    if (romPath.empty()) {
        std::cerr << "Usage: " << argv[0] << " [-s <save.dat>] <rom.gba>\n";
        return 1;
    }

    std::vector<uint8_t> rom;
    if (!readFile(romPath.c_str(), &rom)) {
        std::cerr << "Failed to read GBA ROM: " << romPath << '\n';
        return 1;
    }

    mGBAHelper gba;
    gba.setSramPath(sramPath);
    if (!gba.load(rom.data(), rom.size())) {
        std::cerr << "Failed to load GBA ROM: " << romPath << '\n';
        return 1;
    }

    DewpointRuntime dewpoint(gba, [](const char* message) {
        std::cerr << "[Steam] " << message << '\n';
    });
    dewpoint.initialize();

    ScopedSdl sdl;
    if (!sdl) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return 1;
    }

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_Window* window = SDL_CreateWindow(
        "mGBA SDL2",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        gba.getVramWidth() * WINDOW_SCALE,
        gba.getVramHeight() * WINDOW_SCALE,
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return 1;
    }
    dewpoint.setFullscreenCallbacks(
        [window](bool fullscreen) {
            const Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
            if (SDL_SetWindowFullscreen(window, flags) != 0) {
                std::cerr << "SDL_SetWindowFullscreen failed: " << SDL_GetError() << '\n';
            }
            return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        },
        [window]() {
            return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP) != 0;
        });

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
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
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        return 1;
    }
    SDL_PauseAudioDevice(audioDevice, 0);

    bool running = true;
    int exitCode = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                gba.keyState = {};
            } else if (event.type == SDL_KEYDOWN) {
                const bool command = (event.key.keysym.mod & KMOD_GUI) != 0;
                if (command && event.key.keysym.sym == SDLK_q) {
                    running = false;
                } else if (command && event.key.keysym.sym == SDLK_r && !event.key.repeat) {
                    gba.reset();
                    SDL_ClearQueuedAudio(audioDevice);
                } else if (!command) {
                    updateKeyState(&gba.keyState, event.key.keysym.sym, true);
                }
            } else if (event.type == SDL_KEYUP) {
                updateKeyState(&gba.keyState, event.key.keysym.sym, false);
            }
        }
        if (!running) {
            break;
        }

        dewpoint.tick();
        gba.tick();
        if (dewpoint.takeExitRequest(&exitCode)) {
            running = false;
            break;
        }

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
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    return exitCode;
}
