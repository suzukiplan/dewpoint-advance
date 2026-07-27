/**
 * Dewpoint Advance Runtime (Windows / DirectX)
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#define DIRECTINPUT_VERSION 0x0800
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "dewpoint_runtime.h"
#include "dewpoint_define.h"
#include "mgbahelper.h"
#include "pathutil.h"
#include "steam.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <mmsystem.h>
#include <dsound.h>

#include <algorithm>
#include <cstdarg>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

#include <mgba/core/log.h>

extern "C" {
extern const uint8_t game_rom[];
extern const size_t game_rom_size;
}

namespace
{
constexpr wchar_t WINDOW_CLASS_NAME[] = L"DewpointAdvanceWindow";
constexpr int WINDOW_SCALE = 3;
constexpr int WINDOW_ASPECT_WIDTH = 3;
constexpr int WINDOW_ASPECT_HEIGHT = 2;
constexpr int WINDOW_MIN_WIDTH = 240;
constexpr int WINDOW_MIN_HEIGHT = 160;
constexpr DWORD AUDIO_FREQUENCY = 44100;
constexpr DWORD AUDIO_CHANNELS = 2;
constexpr DWORD AUDIO_BYTES_PER_SAMPLE = sizeof(int16_t);
constexpr DWORD AUDIO_FRAME_BYTES = AUDIO_CHANNELS * AUDIO_BYTES_PER_SAMPLE;
constexpr DWORD AUDIO_BUFFER_BYTES = AUDIO_FREQUENCY * AUDIO_FRAME_BYTES;
constexpr DWORD AUDIO_LATENCY_BYTES = AUDIO_FREQUENCY * AUDIO_FRAME_BYTES / 20;
// One GBA frame is 280896 cycles of the 16.78 MHz master clock.
constexpr double GBA_MASTER_CLOCK_HZ = 16777216.0;
constexpr double GBA_CYCLES_PER_FRAME = 280896.0;
constexpr double GBA_FRAME_RATE = GBA_MASTER_CLOCK_HZ / GBA_CYCLES_PER_FRAME;
constexpr int MAX_EMULATION_CATCH_UP_FRAMES = 4;

const char* CRT_PIXEL_SHADER = R"HLSL(
sampler2D sourceTexture : register(s0);
float4 dimensions : register(c0);

static const float PI = 3.14159265358979323846;
static const float CRT_GAMMA = 2.1;
static const float DISPLAY_GAMMA = 2.2;
static const float CURVATURE = 0.02;
static const float SCANLINE_WEIGHT = 0.30;
static const float DOT_MASK_STRENGTH = 0.30;
static const float2 CONTENT_SCALE = float2(1.0, 1.04);

float2 warp(float2 coord)
{
    float2 centered = coord * 2.0 - 1.0;
    centered = float2(
        centered.x * (1.0 + CURVATURE * centered.y * centered.y),
        centered.y * (1.0 + CURVATURE * centered.x * centered.x));
    return centered * 0.5 + 0.5;
}

float roundedScreenMask(float2 coord)
{
    float2 edgeDistance = abs(coord - float2(0.5, 0.5)) - float2(0.475, 0.4625);
    float outsideDistance =
        length(max(edgeDistance, float2(0.0, 0.0))) +
        min(max(edgeDistance.x, edgeDistance.y), 0.0) -
        0.025;
    float antialiasWidth = 2.0 / min(dimensions.z, dimensions.w);
    return 1.0 - smoothstep(-antialiasWidth, antialiasWidth, outsideDistance);
}

float lanczos2(float distance)
{
    distance = abs(distance);
    if (distance < 0.00001) {
        return 1.0;
    }
    if (distance >= 2.0) {
        return 0.0;
    }
    float piDistance = PI * distance;
    return 2.0 * sin(piDistance) * sin(piDistance * 0.5) /
        (piDistance * piDistance);
}

float3 sampleLinear(float2 texel)
{
    float2 coord = (texel + float2(0.5, 0.5)) / dimensions.xy;
    return pow(tex2D(sourceTexture, coord).rgb, float3(CRT_GAMMA, CRT_GAMMA, CRT_GAMMA));
}

float3 sampleScanline(float2 baseTexel, float rowOffset, float4 coefficients)
{
    float3 color =
        sampleLinear(baseTexel + float2(-1.0, rowOffset)) * coefficients.x +
        sampleLinear(baseTexel + float2( 0.0, rowOffset)) * coefficients.y +
        sampleLinear(baseTexel + float2( 1.0, rowOffset)) * coefficients.z +
        sampleLinear(baseTexel + float2( 2.0, rowOffset)) * coefficients.w;
    return saturate(color);
}

float3 scanlineBeam(float distance, float3 color)
{
    float3 intensity = sqrt(color);
    float3 beamWidth = lerp(
        float3(SCANLINE_WEIGHT * 0.65, SCANLINE_WEIGHT * 0.65, SCANLINE_WEIGHT * 0.65),
        float3(SCANLINE_WEIGHT * 1.15, SCANLINE_WEIGHT * 1.15, SCANLINE_WEIGHT * 1.15),
        intensity);
    float3 scaledDistance = distance / beamWidth;
    float3 peak = lerp(float3(0.95, 0.95, 0.95), float3(1.15, 1.15, 1.15), intensity);
    return peak * exp(-0.5 * scaledDistance * scaledDistance);
}

float4 main(float2 texCoord : TEXCOORD0, float2 pixelPosition : VPOS) : COLOR0
{
    float2 screenCoord = warp(texCoord);
    float2 coord = (screenCoord - 0.5) * CONTENT_SCALE + 0.5;
    if (coord.x < 0.0 || coord.x > 1.0 ||
        coord.y < 0.0 || coord.y > 1.0) {
        return float4(0.0, 0.0, 0.0, 1.0);
    }

    float2 texelPosition = coord * dimensions.xy - 0.5;
    float2 baseTexel = floor(texelPosition);
    float2 subTexel = frac(texelPosition);
    float4 coefficients = float4(
        lanczos2(1.0 + subTexel.x),
        lanczos2(subTexel.x),
        lanczos2(1.0 - subTexel.x),
        lanczos2(2.0 - subTexel.x));
    coefficients /= dot(coefficients, float4(1.0, 1.0, 1.0, 1.0));

    float3 currentLine = sampleScanline(baseTexel, 0.0, coefficients);
    float3 nextLine = sampleScanline(baseTexel, 1.0, coefficients);
    float footprint = dimensions.y / dimensions.w;
    float sampleOffset = footprint / 3.0;
    float3 currentWeight =
        (scanlineBeam(abs(subTexel.y - sampleOffset), currentLine) +
         scanlineBeam(subTexel.y, currentLine) +
         scanlineBeam(subTexel.y + sampleOffset, currentLine)) / 3.0;
    float3 nextWeight =
        (scanlineBeam(abs(1.0 - subTexel.y + sampleOffset), nextLine) +
         scanlineBeam(1.0 - subTexel.y, nextLine) +
         scanlineBeam(abs(1.0 - subTexel.y - sampleOffset), nextLine)) / 3.0;

    float3 color = currentLine * currentWeight + nextLine * nextWeight;
    float maskPhase = fmod(floor(pixelPosition.x), 2.0);
    float3 apertureMask = lerp(
        float3(1.0, 1.0 - DOT_MASK_STRENGTH, 1.0),
        float3(1.0 - DOT_MASK_STRENGTH, 1.0, 1.0 - DOT_MASK_STRENGTH),
        maskPhase);
    color *= apertureMask;
    color = pow(
        saturate(color),
        float3(
            1.0 / DISPLAY_GAMMA,
            1.0 / DISPLAY_GAMMA,
            1.0 / DISPLAY_GAMMA));
    color *= roundedScreenMask(screenCoord);
    return float4(color, 1.0);
}
)HLSL";

static_assert(
    WINDOW_MIN_WIDTH * WINDOW_ASPECT_HEIGHT == WINDOW_MIN_HEIGHT * WINDOW_ASPECT_WIDTH,
    "Minimum window size must match the window aspect ratio");

std::mutex logMutex;
std::string logPath;

void enablePerMonitorDpiAwareness()
{
    // Keep Win32 client coordinates, the D3D back buffer, and Steam overlay UI in
    // physical pixels. Resolve the API dynamically so the executable still starts
    // on Windows versions predating per-monitor-v2 DPI awareness.
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    if (!user32) {
        return;
    }

    using SetProcessDpiAwarenessContextFunction = BOOL(WINAPI*)(HANDLE);
    const auto setDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFunction>(
        GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
    if (setDpiAwarenessContext &&
        setDpiAwarenessContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)))) {
        return;
    }

    using SetProcessDpiAwareFunction = BOOL(WINAPI*)();
    const auto setDpiAware = reinterpret_cast<SetProcessDpiAwareFunction>(
        GetProcAddress(user32, "SetProcessDPIAware"));
    if (setDpiAware) {
        setDpiAware();
    }
}

std::string currentDirectory()
{
    const DWORD required = GetCurrentDirectoryA(0, nullptr);
    if (!required) {
        return ".";
    }
    std::vector<char> buffer(required);
    if (!GetCurrentDirectoryA(required, buffer.data())) {
        return ".";
    }
    return buffer.data();
}

bool getHighScoreStorageDirectory(std::string* directory)
{
    if (!directory) {
        return false;
    }
    char localAppData[MAX_PATH]{};
    if (SHGetFolderPathA(
            nullptr,
            CSIDL_LOCAL_APPDATA | CSIDL_FLAG_CREATE,
            nullptr,
            SHGFP_TYPE_CURRENT,
            localAppData) != S_OK) {
        return false;
    }

    std::string safeAppName = APP_NAME;
    uint32_t appNameHash = 2166136261u;
    for (const unsigned char c : safeAppName) {
        appNameHash = (appNameHash ^ c) * 16777619u;
    }
    std::replace_if(safeAppName.begin(), safeAppName.end(), [](unsigned char c) {
        return !std::isalnum(c) && c != ' ' && c != '-' && c != '_' && c != '.';
    }, '_');
    if (safeAppName.empty() || safeAppName == "." || safeAppName == "..") {
        safeAppName = "DewpointGame";
    }
    char hashSuffix[16]{};
    // mGBA's compatibility header maps the unqualified snprintf token to
    // _snprintf on MSVC. Use the bounds-checked CRT function directly so the
    // macro cannot turn std::snprintf into the nonexistent std::_snprintf.
    sprintf_s(hashSuffix, sizeof(hashSuffix), "-%08X", appNameHash);
    safeAppName += hashSuffix;

    *directory = DewpointPath::join(
        DewpointPath::join(DewpointPath::join(localAppData, "SUZUKI PLAN"), safeAppName),
        "leaderboard-cache");
    const int result = SHCreateDirectoryExA(nullptr, directory->c_str(), nullptr);
    return result == ERROR_SUCCESS || result == ERROR_ALREADY_EXISTS || result == ERROR_FILE_EXISTS;
}

bool selectLogPath(const std::string& directory, bool truncate)
{
    std::lock_guard<std::mutex> guard(logMutex);
    const std::string candidate =
        DewpointPath::join(directory.empty() ? currentDirectory() : directory, "log.txt");
    FILE* file = nullptr;
    fopen_s(&file, candidate.c_str(), truncate ? "w" : "a");
    if (!file) {
        return false;
    }
    fclose(file);
    logPath = candidate;
    return true;
}

void writeLogV(const char* format, va_list arguments)
{
    std::lock_guard<std::mutex> guard(logMutex);
    if (logPath.empty()) {
        return;
    }

    FILE* file = nullptr;
    fopen_s(&file, logPath.c_str(), "a");
    if (!file) {
        return;
    }

    std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    std::fprintf(
        file,
        "%04d/%02d/%02d %02d:%02d:%02d ",
        local.tm_year + 1900,
        local.tm_mon + 1,
        local.tm_mday,
        local.tm_hour,
        local.tm_min,
        local.tm_sec);
    std::vfprintf(file, format, arguments);
    std::fputc('\n', file);
    std::fclose(file);
}

void writeLog(const char* format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    writeLogV(format, arguments);
    va_end(arguments);
}

void reportError(const char* message)
{
    writeLog("%s", message);
    const int required = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
    std::vector<wchar_t> wideMessage(required > 0 ? required : 1);
    if (required > 0) {
        MultiByteToWideChar(CP_UTF8, 0, message, -1, wideMessage.data(), required);
    } else {
        wideMessage[0] = L'\0';
    }
    MessageBoxW(nullptr, wideMessage.data(), L"Dewpoint Advance", MB_OK | MB_ICONERROR);
}

class ScopedLogger
{
  private:
    mLogger logger;
    mLogFilter filter;

    static void log(
        mLogger*,
        int category,
        mLogLevel level,
        const char* format,
        va_list arguments)
    {
        char message[2048];
        vsnprintf_s(message, sizeof(message), _TRUNCATE, format, arguments);
        const char* categoryName = mLogCategoryName(category);
        writeLog("[mGBA:%s:%02X] %s", categoryName ? categoryName : "unknown", level, message);
    }

  public:
    ScopedLogger()
        : logger{}, filter{}
    {
        mLogFilterInit(&filter);
        filter.defaultLevels = mLOG_FATAL | mLOG_ERROR | mLOG_WARN | mLOG_GAME_ERROR;
        logger.log = log;
        logger.filter = &filter;
        mLogSetDefaultLogger(&logger);
    }

    ~ScopedLogger()
    {
        mLogSetDefaultLogger(nullptr);
        mLogFilterDeinit(&filter);
    }
};

struct WindowConfig {
    int32_t fullscreen;
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;
};

static_assert(sizeof(WindowConfig) == 20, "WindowConfig must use five 4-byte fields");

WindowConfig defaultWindowConfig()
{
    return WindowConfig{
        -1,
        GBA_VRAM_WIDTH * WINDOW_SCALE,
        GBA_VRAM_HEIGHT * WINDOW_SCALE,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
    };
}

WindowConfig loadWindowConfig(const std::string& path)
{
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        bool exists = false;
        bool isDirectory = false;
        if (!DewpointPath::inspect(path, &exists, &isDirectory, nullptr) || exists) {
            writeLog("Failed to read window configuration: %s", path.c_str());
        }
        return defaultWindowConfig();
    }

    WindowConfig config{};
    if (input.tellg() != static_cast<std::streamsize>(sizeof(config))) {
        writeLog("Invalid window configuration size: %s", path.c_str());
        return defaultWindowConfig();
    }
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(&config), sizeof(config)) ||
        (config.fullscreen != -1 && config.fullscreen != 0) || config.width <= 0 || config.height <= 0) {
        writeLog("Invalid window configuration: %s", path.c_str());
        return defaultWindowConfig();
    }
    const int64_t aspectUnits = std::min<int64_t>(
        std::max<int64_t>(
            WINDOW_MIN_WIDTH / WINDOW_ASPECT_WIDTH,
            (static_cast<int64_t>(config.width) + WINDOW_ASPECT_WIDTH / 2) /
                WINDOW_ASPECT_WIDTH),
        std::numeric_limits<int>::max() / WINDOW_ASPECT_WIDTH);
    config.width = static_cast<int32_t>(aspectUnits * WINDOW_ASPECT_WIDTH);
    config.height = static_cast<int32_t>(aspectUnits * WINDOW_ASPECT_HEIGHT);
    return config;
}

bool saveWindowConfig(
    const std::string& path,
    bool fullscreen,
    int windowedWidth,
    int windowedHeight,
    int windowedX,
    int windowedY)
{
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

bool getSteamInstallDirectory(std::string* installDirectory)
{
    auto* apps = SteamApps();
    auto* utils = SteamUtils();
    if (!apps || !utils) {
        writeLog("Failed to access Steam installation information");
        return false;
    }
    std::vector<char> pathBuffer(4096);
    if (apps->GetAppInstallDir(utils->GetAppID(), pathBuffer.data(), pathBuffer.size()) == 0) {
        writeLog("Failed to get Steam App installation directory");
        return false;
    }
    *installDirectory = pathBuffer.data();
    return true;
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
        writeLog(
            "Failed to create default save directory: %s: %s",
            saveDirectory.c_str(),
            errorMessage.c_str());
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

void useCurrentDirectoryForDefaultPaths(
    const std::string& directory,
    bool usesDefaultSramPath,
    bool usesDefaultConfigPath,
    std::string* sramPath,
    std::string* configPath)
{
    if (usesDefaultSramPath) {
        *sramPath = DewpointPath::join(directory, "save.dat");
    }
    if (usesDefaultConfigPath) {
        *configPath = DewpointPath::join(directory, "config.dat");
    }
}

class Direct3DRenderer
{
  private:
    HWND window;
    IDirect3D9* d3d;
    IDirect3DDevice9* device;
    IDirect3DTexture9* texture;
    IDirect3DPixelShader9* crtPixelShader;
    D3DPRESENT_PARAMETERS parameters;
    bool crtEnabled;
    bool resetRequested;

    struct Vertex {
        float x;
        float y;
        float z;
        float rhw;
        float u;
        float v;
    };

    static constexpr DWORD VERTEX_FORMAT = D3DFVF_XYZRHW | D3DFVF_TEX1;

    bool updateBackBufferSize()
    {
        RECT client{};
        if (!GetClientRect(window, &client)) {
            writeLog("GetClientRect failed while sizing the Direct3D back buffer: %lu", GetLastError());
            return false;
        }
        const LONG width = client.right - client.left;
        const LONG height = client.bottom - client.top;
        if (width <= 0 || height <= 0) {
            return false;
        }
        parameters.BackBufferWidth = static_cast<UINT>(width);
        parameters.BackBufferHeight = static_cast<UINT>(height);
        return true;
    }

    bool updateViewport()
    {
        D3DVIEWPORT9 viewport{};
        viewport.Width = parameters.BackBufferWidth;
        viewport.Height = parameters.BackBufferHeight;
        viewport.MaxZ = 1.0f;
        const HRESULT result = device->SetViewport(&viewport);
        if (FAILED(result)) {
            writeLog("IDirect3DDevice9::SetViewport failed: %08lX", result);
            return false;
        }
        return true;
    }

    bool createTexture()
    {
        const HRESULT result = device->CreateTexture(
            GBA_VRAM_WIDTH,
            GBA_VRAM_HEIGHT,
            1,
            0,
            D3DFMT_X8R8G8B8,
            D3DPOOL_MANAGED,
            &texture,
            nullptr);
        if (FAILED(result)) {
            writeLog("IDirect3DDevice9::CreateTexture failed: %08lX", result);
            return false;
        }
        return true;
    }

    bool createCrtPixelShader()
    {
        D3DCAPS9 capabilities{};
        HRESULT result = device->GetDeviceCaps(&capabilities);
        if (FAILED(result)) {
            writeLog("IDirect3DDevice9::GetDeviceCaps failed: %08lX", result);
            return false;
        }
        if (capabilities.PixelShaderVersion < D3DPS_VERSION(3, 0)) {
            writeLog(
                "The CRT filter requires Pixel Shader 3.0; available version is %u.%u",
                D3DSHADER_VERSION_MAJOR(capabilities.PixelShaderVersion),
                D3DSHADER_VERSION_MINOR(capabilities.PixelShaderVersion));
            return false;
        }

        // Resolve the compiler at runtime so the normal, unfiltered renderer
        // does not gain a new DLL dependency. Search System32 explicitly to
        // prevent an untrusted DLL beside the executable from being loaded.
        const wchar_t* compilerNames[] = {
            L"d3dcompiler_47.dll",
            L"d3dcompiler_46.dll",
            L"d3dcompiler_43.dll",
        };
        wchar_t systemDirectory[MAX_PATH]{};
        const UINT systemDirectoryLength =
            GetSystemDirectoryW(systemDirectory, MAX_PATH);
        if (!systemDirectoryLength) {
            writeLog("Failed to locate the Windows system directory: %lu", GetLastError());
            return false;
        }
        if (systemDirectoryLength >= MAX_PATH) {
            writeLog("The Windows system directory path is too long");
            return false;
        }
        std::wstring compilerDirectory(systemDirectory, systemDirectoryLength);
        if (!compilerDirectory.empty() && compilerDirectory.back() != L'\\') {
            compilerDirectory += L'\\';
        }

        HMODULE compilerModule = nullptr;
        for (const wchar_t* compilerName : compilerNames) {
            const std::wstring compilerPath = compilerDirectory + compilerName;
            compilerModule = LoadLibraryW(compilerPath.c_str());
            if (compilerModule) {
                break;
            }
        }
        if (!compilerModule) {
            writeLog("Failed to load a Direct3D shader compiler DLL");
            return false;
        }

        using D3DCompileFunction = decltype(&D3DCompile);
        const auto compile = reinterpret_cast<D3DCompileFunction>(
            GetProcAddress(compilerModule, "D3DCompile"));
        if (!compile) {
            writeLog("Failed to resolve D3DCompile");
            FreeLibrary(compilerModule);
            return false;
        }

        ID3DBlob* bytecode = nullptr;
        ID3DBlob* errors = nullptr;
        result = compile(
            CRT_PIXEL_SHADER,
            std::strlen(CRT_PIXEL_SHADER),
            "Dewpoint CRT filter",
            nullptr,
            nullptr,
            "main",
            "ps_3_0",
            D3DCOMPILE_OPTIMIZATION_LEVEL3,
            0,
            &bytecode,
            &errors);
        if (FAILED(result)) {
            if (errors && errors->GetBufferPointer()) {
                writeLog(
                    "Failed to compile the CRT pixel shader: %s",
                    static_cast<const char*>(errors->GetBufferPointer()));
            } else {
                writeLog("D3DCompile failed for the CRT pixel shader: %08lX", result);
            }
        } else {
            result = device->CreatePixelShader(
                static_cast<const DWORD*>(bytecode->GetBufferPointer()),
                &crtPixelShader);
            if (FAILED(result)) {
                writeLog("IDirect3DDevice9::CreatePixelShader failed: %08lX", result);
            }
        }

        if (errors) {
            errors->Release();
        }
        if (bytecode) {
            bytecode->Release();
        }
        FreeLibrary(compilerModule);
        return SUCCEEDED(result) && crtPixelShader;
    }

    bool resetDevice()
    {
        const HRESULT cooperative = device->TestCooperativeLevel();
        if (cooperative == D3DERR_DEVICELOST) {
            return true;
        }
        if (cooperative != D3D_OK && cooperative != D3DERR_DEVICENOTRESET) {
            writeLog("IDirect3DDevice9::TestCooperativeLevel failed: %08lX", cooperative);
            return false;
        }
        if (!updateBackBufferSize()) {
            return true;
        }
        const HRESULT result = device->Reset(&parameters);
        if (result == D3DERR_DEVICELOST) {
            return true;
        }
        if (FAILED(result)) {
            writeLog("IDirect3DDevice9::Reset failed: %08lX", result);
            return false;
        }
        if (!updateViewport()) {
            return false;
        }
        resetRequested = false;
        return true;
    }

  public:
    Direct3DRenderer()
        : window(nullptr)
        , d3d(nullptr)
        , device(nullptr)
        , texture(nullptr)
        , crtPixelShader(nullptr)
        , parameters{}
        , crtEnabled(false)
        , resetRequested(false)
    {
    }

    ~Direct3DRenderer() { shutdown(); }

    bool initialize(HWND targetWindow, bool enableCrt)
    {
        window = targetWindow;
        crtEnabled = enableCrt;
        d3d = Direct3DCreate9(D3D_SDK_VERSION);
        if (!d3d) {
            writeLog("Direct3DCreate9 failed");
            return false;
        }

        parameters.Windowed = TRUE;
        parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
        parameters.BackBufferFormat = D3DFMT_UNKNOWN;
        parameters.BackBufferCount = 1;
        parameters.hDeviceWindow = window;
        parameters.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
        if (!updateBackBufferSize()) {
            writeLog("Failed to determine the initial Direct3D back buffer size");
            return false;
        }

        HRESULT result = d3d->CreateDevice(
            D3DADAPTER_DEFAULT,
            D3DDEVTYPE_HAL,
            window,
            D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &parameters,
            &device);
        if (FAILED(result)) {
            result = d3d->CreateDevice(
                D3DADAPTER_DEFAULT,
                D3DDEVTYPE_HAL,
                window,
                D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                &parameters,
                &device);
        }
        if (FAILED(result)) {
            writeLog("IDirect3D9::CreateDevice failed: %08lX", result);
            return false;
        }
        if (!updateViewport()) {
            return false;
        }
        if (!createTexture()) {
            return false;
        }
        if (crtEnabled && !createCrtPixelShader()) {
            return false;
        }
        writeLog(
            "Direct3D 9 initialized with video filter: %s",
            crtEnabled ? "crt" : "no");
        return true;
    }

    void requestReset() { resetRequested = true; }

    bool render(const uint32_t* pixels)
    {
        if (!device || !texture || IsIconic(window)) {
            return true;
        }
        if (resetRequested && !resetDevice()) {
            return false;
        }
        if (device->TestCooperativeLevel() == D3DERR_DEVICELOST) {
            resetRequested = true;
            return true;
        }

        D3DLOCKED_RECT locked{};
        HRESULT result = texture->LockRect(0, &locked, nullptr, 0);
        if (FAILED(result)) {
            writeLog("IDirect3DTexture9::LockRect failed: %08lX", result);
            return false;
        }
        for (int y = 0; y < GBA_VRAM_HEIGHT; ++y) {
            auto* destination = reinterpret_cast<uint32_t*>(
                static_cast<uint8_t*>(locked.pBits) + y * locked.Pitch);
            const uint32_t* source = pixels + y * GBA_VRAM_WIDTH;
            for (int x = 0; x < GBA_VRAM_WIDTH; ++x) {
                const uint32_t pixel = source[x];
                destination[x] = 0xFF000000U | (pixel & 0x0000FF00U) |
                                 ((pixel & 0x000000FFU) << 16) |
                                 ((pixel & 0x00FF0000U) >> 16);
            }
        }
        texture->UnlockRect(0);

        RECT client{};
        GetClientRect(window, &client);
        const float clientWidth = static_cast<float>(client.right - client.left);
        const float clientHeight = static_cast<float>(client.bottom - client.top);
        if (clientWidth <= 0.0f || clientHeight <= 0.0f) {
            return true;
        }
        const float scale = std::min(
            clientWidth / static_cast<float>(GBA_VRAM_WIDTH),
            clientHeight / static_cast<float>(GBA_VRAM_HEIGHT));
        const float width = GBA_VRAM_WIDTH * scale;
        const float height = GBA_VRAM_HEIGHT * scale;
        const float left = (clientWidth - width) * 0.5f - 0.5f;
        const float top = (clientHeight - height) * 0.5f - 0.5f;
        const float right = left + width;
        const float bottom = top + height;
        const Vertex vertices[] = {
            {left, top, 0.0f, 1.0f, 0.0f, 0.0f},
            {right, top, 0.0f, 1.0f, 1.0f, 0.0f},
            {left, bottom, 0.0f, 1.0f, 0.0f, 1.0f},
            {right, bottom, 0.0f, 1.0f, 1.0f, 1.0f},
        };

        device->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0);
        result = device->BeginScene();
        if (SUCCEEDED(result)) {
            device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
            device->SetRenderState(D3DRS_LIGHTING, FALSE);
            device->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
            device->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
            device->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
            device->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
            device->SetTexture(0, texture);
            device->SetFVF(VERTEX_FORMAT);
            result = device->SetPixelShader(crtEnabled ? crtPixelShader : nullptr);
            if (SUCCEEDED(result) && crtEnabled) {
                const float shaderDimensions[] = {
                    static_cast<float>(GBA_VRAM_WIDTH),
                    static_cast<float>(GBA_VRAM_HEIGHT),
                    width,
                    height,
                };
                result = device->SetPixelShaderConstantF(0, shaderDimensions, 1);
            }
            if (SUCCEEDED(result)) {
                result = device->DrawPrimitiveUP(
                    D3DPT_TRIANGLESTRIP,
                    2,
                    vertices,
                    sizeof(Vertex));
            }
            device->EndScene();
        }
        if (FAILED(result)) {
            writeLog("Direct3D draw failed: %08lX", result);
            return false;
        }

        result = device->Present(nullptr, nullptr, nullptr, nullptr);
        if (result == D3DERR_DEVICELOST) {
            resetRequested = true;
            return true;
        }
        if (FAILED(result)) {
            writeLog("IDirect3DDevice9::Present failed: %08lX", result);
            return false;
        }
        return true;
    }

    void shutdown()
    {
        if (crtPixelShader) {
            crtPixelShader->Release();
            crtPixelShader = nullptr;
        }
        if (texture) {
            texture->Release();
            texture = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
        if (d3d) {
            d3d->Release();
            d3d = nullptr;
        }
    }
};

class DirectSoundOutput
{
  private:
    IDirectSound8* directSound;
    IDirectSoundBuffer8* buffer;
    DWORD writeOffset;
    DWORD lastPlayCursor;
    DWORD bufferedBytes;
    bool playing;

    bool clearBuffer()
    {
        void* first = nullptr;
        void* second = nullptr;
        DWORD firstSize = 0;
        DWORD secondSize = 0;
        HRESULT result = buffer->Lock(
            0,
            AUDIO_BUFFER_BYTES,
            &first,
            &firstSize,
            &second,
            &secondSize,
            DSBLOCK_ENTIREBUFFER);
        if (result == DSERR_BUFFERLOST) {
            buffer->Restore();
            result = buffer->Lock(
                0,
                AUDIO_BUFFER_BYTES,
                &first,
                &firstSize,
                &second,
                &secondSize,
                DSBLOCK_ENTIREBUFFER);
        }
        if (FAILED(result)) {
            writeLog("IDirectSoundBuffer8::Lock failed while clearing: %08lX", result);
            return false;
        }
        std::memset(first, 0, firstSize);
        if (second && secondSize) {
            std::memset(second, 0, secondSize);
        }
        buffer->Unlock(first, firstSize, second, secondSize);
        return true;
    }

    bool updatePlayback()
    {
        DWORD playCursor = 0;
        const HRESULT result = buffer->GetCurrentPosition(&playCursor, nullptr);
        if (FAILED(result)) {
            writeLog("IDirectSoundBuffer8::GetCurrentPosition failed: %08lX", result);
            return false;
        }
        const DWORD played = playCursor >= lastPlayCursor
                                 ? playCursor - lastPlayCursor
                                 : AUDIO_BUFFER_BYTES - lastPlayCursor + playCursor;
        bufferedBytes = played >= bufferedBytes ? 0 : bufferedBytes - played;
        lastPlayCursor = playCursor;
        return true;
    }

  public:
    DirectSoundOutput()
        : directSound(nullptr), buffer(nullptr), writeOffset(0), lastPlayCursor(0), bufferedBytes(0),
          playing(false)
    {
    }

    ~DirectSoundOutput() { shutdown(); }

    bool initialize(HWND window)
    {
        HRESULT result = DirectSoundCreate8(nullptr, &directSound, nullptr);
        if (FAILED(result)) {
            writeLog("DirectSoundCreate8 failed: %08lX", result);
            return false;
        }
        result = directSound->SetCooperativeLevel(window, DSSCL_NORMAL);
        if (FAILED(result)) {
            writeLog("IDirectSound8::SetCooperativeLevel failed: %08lX", result);
            return false;
        }

        WAVEFORMATEX format{};
        format.wFormatTag = WAVE_FORMAT_PCM;
        format.nChannels = AUDIO_CHANNELS;
        format.nSamplesPerSec = AUDIO_FREQUENCY;
        format.wBitsPerSample = AUDIO_BYTES_PER_SAMPLE * 8;
        format.nBlockAlign = AUDIO_FRAME_BYTES;
        format.nAvgBytesPerSec = AUDIO_FREQUENCY * AUDIO_FRAME_BYTES;

        DSBUFFERDESC description{};
        description.dwSize = sizeof(description);
        description.dwFlags = DSBCAPS_GLOBALFOCUS | DSBCAPS_GETCURRENTPOSITION2;
        description.dwBufferBytes = AUDIO_BUFFER_BYTES;
        description.lpwfxFormat = &format;

        IDirectSoundBuffer* temporary = nullptr;
        result = directSound->CreateSoundBuffer(&description, &temporary, nullptr);
        if (FAILED(result)) {
            writeLog("IDirectSound8::CreateSoundBuffer failed: %08lX", result);
            return false;
        }
        result = temporary->QueryInterface(IID_IDirectSoundBuffer8, reinterpret_cast<void**>(&buffer));
        temporary->Release();
        if (FAILED(result)) {
            writeLog("QueryInterface(IID_IDirectSoundBuffer8) failed: %08lX", result);
            return false;
        }
        if (!reset()) {
            return false;
        }
        writeLog("DirectSound 8 initialized");
        return true;
    }

    bool reset()
    {
        if (!buffer) {
            return false;
        }
        buffer->Stop();
        if (!clearBuffer()) {
            return false;
        }
        if (FAILED(buffer->SetCurrentPosition(0))) {
            return false;
        }
        writeOffset = AUDIO_LATENCY_BYTES;
        lastPlayCursor = 0;
        bufferedBytes = AUDIO_LATENCY_BYTES;
        const HRESULT result = buffer->Play(0, 0, DSBPLAY_LOOPING);
        if (FAILED(result)) {
            writeLog("IDirectSoundBuffer8::Play failed: %08lX", result);
            return false;
        }
        playing = true;
        return true;
    }

    void pause(bool paused)
    {
        if (!buffer) {
            return;
        }
        if (paused) {
            buffer->Stop();
            playing = false;
        } else if (!playing) {
            reset();
        }
    }

    bool queue(const void* samples, DWORD size)
    {
        if (!buffer || !samples || !size) {
            return true;
        }
        if (size > AUDIO_BUFFER_BYTES - AUDIO_LATENCY_BYTES) {
            writeLog("Audio packet is too large: %lu", size);
            return false;
        }
        if (!playing && !reset()) {
            return false;
        }
        if (!updatePlayback()) {
            return false;
        }
        if (!bufferedBytes && !reset()) {
            return false;
        }
        while (AUDIO_BUFFER_BYTES - bufferedBytes < size) {
            Sleep(1);
            if (!updatePlayback()) {
                return false;
            }
        }

        void* first = nullptr;
        void* second = nullptr;
        DWORD firstSize = 0;
        DWORD secondSize = 0;
        HRESULT result = buffer->Lock(
            writeOffset,
            size,
            &first,
            &firstSize,
            &second,
            &secondSize,
            0);
        if (result == DSERR_BUFFERLOST) {
            if (!reset()) {
                return false;
            }
            result = buffer->Lock(
                writeOffset,
                size,
                &first,
                &firstSize,
                &second,
                &secondSize,
                0);
        }
        if (FAILED(result)) {
            writeLog("IDirectSoundBuffer8::Lock failed: %08lX", result);
            return false;
        }
        std::memcpy(first, samples, firstSize);
        if (second && secondSize) {
            std::memcpy(second, static_cast<const uint8_t*>(samples) + firstSize, secondSize);
        }
        result = buffer->Unlock(first, firstSize, second, secondSize);
        if (FAILED(result)) {
            writeLog("IDirectSoundBuffer8::Unlock failed: %08lX", result);
            return false;
        }
        writeOffset = (writeOffset + size) % AUDIO_BUFFER_BYTES;
        bufferedBytes += size;
        return true;
    }

    void shutdown()
    {
        if (buffer) {
            buffer->Stop();
            buffer->Release();
            buffer = nullptr;
        }
        if (directSound) {
            directSound->Release();
            directSound = nullptr;
        }
        playing = false;
    }
};

struct WindowState {
    HWND window;
    Direct3DRenderer* renderer;
    DirectSoundOutput* audio;
    mGBAHelper* gba;
    bool running;
    bool fullscreen;
    bool allowWindowed;
    bool paused;
    int windowedWidth;
    int windowedHeight;
    int windowedX;
    int windowedY;
};

WindowState* activeWindow = nullptr;

enum class ResizeDimension {
    Automatic,
    Width,
    Height,
};

void getWindowFrameSize(HWND window, int* width, int* height)
{
    RECT windowRect{};
    RECT clientRect{};
    if (GetWindowRect(window, &windowRect) && GetClientRect(window, &clientRect)) {
        const int windowWidth = windowRect.right - windowRect.left;
        const int windowHeight = windowRect.bottom - windowRect.top;
        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        if (windowWidth > 0 &&
            windowHeight > 0 &&
            windowWidth >= clientWidth &&
            windowHeight >= clientHeight) {
            *width = windowWidth - clientWidth;
            *height = windowHeight - clientHeight;
            return;
        }
    }

    RECT frame{};
    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE));
    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE));
    AdjustWindowRectEx(&frame, style, FALSE, extendedStyle);
    *width = frame.right - frame.left;
    *height = frame.bottom - frame.top;
}

void constrainClientSize(
    int requestedWidth,
    int requestedHeight,
    int previousWidth,
    int previousHeight,
    ResizeDimension dimension,
    int* width,
    int* height)
{
    if (dimension == ResizeDimension::Automatic) {
        const int64_t widthChange =
            std::abs(static_cast<int64_t>(requestedWidth) - previousWidth) * WINDOW_ASPECT_HEIGHT;
        const int64_t heightChange =
            std::abs(static_cast<int64_t>(requestedHeight) - previousHeight) * WINDOW_ASPECT_WIDTH;
        dimension = widthChange >= heightChange ? ResizeDimension::Width : ResizeDimension::Height;
    }
    const int aspectDimension =
        dimension == ResizeDimension::Width ? WINDOW_ASPECT_WIDTH : WINDOW_ASPECT_HEIGHT;
    const int requestedDimension = std::max(
        dimension == ResizeDimension::Width ? requestedWidth : requestedHeight,
        dimension == ResizeDimension::Width ? WINDOW_MIN_WIDTH : WINDOW_MIN_HEIGHT);
    const int64_t aspectUnits = std::max<int64_t>(
        WINDOW_MIN_WIDTH / WINDOW_ASPECT_WIDTH,
        (static_cast<int64_t>(requestedDimension) + aspectDimension / 2) / aspectDimension);
    const int64_t constrainedUnits = std::min<int64_t>(
        aspectUnits,
        std::numeric_limits<int>::max() / WINDOW_ASPECT_WIDTH);
    *width = static_cast<int>(constrainedUnits * WINDOW_ASPECT_WIDTH);
    *height = static_cast<int>(constrainedUnits * WINDOW_ASPECT_HEIGHT);
}

bool setClientSize(HWND window, int width, int height)
{
    int frameWidth = 0;
    int frameHeight = 0;
    getWindowFrameSize(window, &frameWidth, &frameHeight);
    const int outerWidth = static_cast<int>(std::min<int64_t>(
        static_cast<int64_t>(width) + frameWidth,
        std::numeric_limits<int>::max()));
    const int outerHeight = static_cast<int>(std::min<int64_t>(
        static_cast<int64_t>(height) + frameHeight,
        std::numeric_limits<int>::max()));
    return SetWindowPos(
        window,
        nullptr,
        0,
        0,
        outerWidth,
        outerHeight,
        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE) != FALSE;
}

void constrainSizingRect(
    WindowState* state,
    WPARAM edge,
    RECT* outer)
{
    int frameWidth = 0;
    int frameHeight = 0;
    getWindowFrameSize(state->window, &frameWidth, &frameHeight);
    const int requestedWidth = outer->right - outer->left - frameWidth;
    const int requestedHeight = outer->bottom - outer->top - frameHeight;

    ResizeDimension dimension = ResizeDimension::Automatic;
    if (edge == WMSZ_LEFT || edge == WMSZ_RIGHT) {
        dimension = ResizeDimension::Width;
    } else if (edge == WMSZ_TOP || edge == WMSZ_BOTTOM) {
        dimension = ResizeDimension::Height;
    }

    int clientWidth = 0;
    int clientHeight = 0;
    constrainClientSize(
        requestedWidth,
        requestedHeight,
        state->windowedWidth,
        state->windowedHeight,
        dimension,
        &clientWidth,
        &clientHeight);
    const int outerWidth = clientWidth + frameWidth;
    const int outerHeight = clientHeight + frameHeight;

    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) {
        outer->left = outer->right - outerWidth;
    } else {
        outer->right = outer->left + outerWidth;
    }
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) {
        outer->top = outer->bottom - outerHeight;
    } else {
        outer->bottom = outer->top + outerHeight;
    }
}

void updateWindowMinMaxInfo(HWND window, MINMAXINFO* info)
{
    int frameWidth = 0;
    int frameHeight = 0;
    getWindowFrameSize(window, &frameWidth, &frameHeight);
    info->ptMinTrackSize.x = WINDOW_MIN_WIDTH + frameWidth;
    info->ptMinTrackSize.y = WINDOW_MIN_HEIGHT + frameHeight;

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    const HMONITOR target = MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY);
    if (!GetMonitorInfoW(target, &monitor)) {
        return;
    }
    const int workWidth = monitor.rcWork.right - monitor.rcWork.left;
    const int workHeight = monitor.rcWork.bottom - monitor.rcWork.top;
    const int clientWidth = std::max(0, workWidth - frameWidth);
    const int clientHeight = std::max(0, workHeight - frameHeight);
    const int aspectUnits = std::max(
        WINDOW_MIN_WIDTH / WINDOW_ASPECT_WIDTH,
        std::min(clientWidth / WINDOW_ASPECT_WIDTH, clientHeight / WINDOW_ASPECT_HEIGHT));
    const int maximumWidth = aspectUnits * WINDOW_ASPECT_WIDTH + frameWidth;
    const int maximumHeight = aspectUnits * WINDOW_ASPECT_HEIGHT + frameHeight;
    info->ptMaxPosition.x =
        monitor.rcWork.left - monitor.rcMonitor.left + (workWidth - maximumWidth) / 2;
    info->ptMaxPosition.y =
        monitor.rcWork.top - monitor.rcMonitor.top + (workHeight - maximumHeight) / 2;
    info->ptMaxSize.x = maximumWidth;
    info->ptMaxSize.y = maximumHeight;
    info->ptMaxTrackSize.x = maximumWidth;
    info->ptMaxTrackSize.y = maximumHeight;
}

void rememberWindowedState(WindowState* state)
{
    if (!state || !state->window || state->fullscreen || IsIconic(state->window)) {
        return;
    }
    RECT client{};
    RECT window{};
    if (GetClientRect(state->window, &client)) {
        state->windowedWidth = client.right - client.left;
        state->windowedHeight = client.bottom - client.top;
    }
    if (GetWindowRect(state->window, &window)) {
        state->windowedX = window.left;
        state->windowedY = window.top;
    }
}

bool setFullscreen(WindowState* state, bool fullscreen)
{
    if (!state || !state->window) {
        return false;
    }
    if (!fullscreen && !state->allowWindowed) {
        return state->fullscreen;
    }
    if (fullscreen == state->fullscreen) {
        return state->fullscreen;
    }

    if (fullscreen) {
        rememberWindowedState(state);
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        const HMONITOR target = MonitorFromWindow(state->window, MONITOR_DEFAULTTOPRIMARY);
        if (!GetMonitorInfoW(target, &monitor)) {
            return state->fullscreen;
        }
        state->fullscreen = true;
        SetWindowLongPtrW(state->window, GWL_STYLE, WS_POPUP);
        SetWindowLongPtrW(state->window, GWL_EXSTYLE, WS_EX_TOPMOST);
        SetWindowPos(
            state->window,
            HWND_TOPMOST,
            monitor.rcMonitor.left,
            monitor.rcMonitor.top,
            monitor.rcMonitor.right - monitor.rcMonitor.left,
            monitor.rcMonitor.bottom - monitor.rcMonitor.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        ShowCursor(FALSE);
    } else {
        SetWindowLongPtrW(state->window, GWL_EXSTYLE, 0);
        SetWindowLongPtrW(state->window, GWL_STYLE, WS_OVERLAPPEDWINDOW);
        RECT outer{0, 0, state->windowedWidth, state->windowedHeight};
        AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
        SetWindowPos(
            state->window,
            HWND_NOTOPMOST,
            state->windowedX,
            state->windowedY,
            outer.right - outer.left,
            outer.bottom - outer.top,
            SWP_FRAMECHANGED | SWP_SHOWWINDOW);
        ShowCursor(TRUE);
        state->fullscreen = false;
        if (!setClientSize(state->window, state->windowedWidth, state->windowedHeight)) {
            writeLog("Failed to restore the windowed client size");
        }
    }
    if (state->renderer) {
        state->renderer->requestReset();
    }
    writeLog("Fullscreen: %s", state->fullscreen ? "enabled" : "disabled");
    return state->fullscreen;
}

LRESULT CALLBACK windowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    WindowState* state = activeWindow;
    switch (message) {
        case WM_CLOSE:
            if (state) {
                state->running = false;
            }
            DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_GETMINMAXINFO:
            if (!state || !state->fullscreen) {
                updateWindowMinMaxInfo(window, reinterpret_cast<MINMAXINFO*>(lParam));
                return 0;
            }
            break;
        case WM_SIZING:
            if (state && !state->fullscreen) {
                constrainSizingRect(state, wParam, reinterpret_cast<RECT*>(lParam));
                return TRUE;
            }
            break;
        case WM_SIZE:
            if (state && !state->fullscreen && wParam != SIZE_MINIMIZED) {
                state->windowedWidth = LOWORD(lParam);
                state->windowedHeight = HIWORD(lParam);
            }
            if (state && state->renderer) {
                state->renderer->requestReset();
            }
            return 0;
        case WM_MOVE:
            rememberWindowedState(state);
            return 0;
        case WM_SYSKEYDOWN:
            if (state && wParam == VK_RETURN && (lParam & (1U << 29)) && !(lParam & (1U << 30))) {
                setFullscreen(state, !state->fullscreen);
                return 0;
            }
            break;
        case WM_KEYDOWN:
            if (state && !(lParam & (1U << 30))) {
                if (wParam == VK_F11) {
                    setFullscreen(state, !state->fullscreen);
                    return 0;
                }
                if (!state->fullscreen && wParam >= '1' && wParam <= '4') {
                    if (IsZoomed(window)) {
                        ShowWindow(window, SW_RESTORE);
                    }
                    const int scale = static_cast<int>(wParam - '0');
                    if (!setClientSize(
                            window,
                            WINDOW_MIN_WIDTH * scale,
                            WINDOW_MIN_HEIGHT * scale)) {
                        writeLog("Failed to set window scale: %d", scale);
                    }
                    return 0;
                }
                if (GetKeyState(VK_CONTROL) & 0x8000) {
                    if (wParam == 'R') {
                        state->gba->reset();
                        state->audio->reset();
                        writeLog("Reset");
                        return 0;
                    }
                    if (wParam == 'P') {
                        state->paused = !state->paused;
                        state->audio->pause(state->paused);
                        writeLog("Paused: %s", state->paused ? "yes" : "no");
                        return 0;
                    }
                }
            }
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

std::wstring applicationName()
{
    const int required = MultiByteToWideChar(CP_UTF8, 0, APP_NAME, -1, nullptr, 0);
    if (required <= 0) {
        return L"Dewpoint Advance";
    }
    std::vector<wchar_t> buffer(required);
    MultiByteToWideChar(CP_UTF8, 0, APP_NAME, -1, buffer.data(), required);
    return buffer.data();
}

HWND createApplicationWindow(HINSTANCE instance, const WindowConfig& config, WindowState* state)
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    windowClass.lpfnWndProc = windowProcedure;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    windowClass.lpszClassName = WINDOW_CLASS_NAME;
    if (!RegisterClassExW(&windowClass)) {
        writeLog("RegisterClassExW failed: %lu", GetLastError());
        return nullptr;
    }

    RECT outer{0, 0, config.width, config.height};
    AdjustWindowRectEx(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0);
    int x = config.x;
    int y = config.y;
    if (x == CW_USEDEFAULT || y == CW_USEDEFAULT) {
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        x = workArea.left + ((workArea.right - workArea.left) - (outer.right - outer.left)) / 2;
        y = workArea.top + ((workArea.bottom - workArea.top) - (outer.bottom - outer.top)) / 2;
    }
    const std::wstring title = applicationName();
    HWND window = CreateWindowExW(
        0,
        WINDOW_CLASS_NAME,
        title.c_str(),
        WS_OVERLAPPEDWINDOW,
        x,
        y,
        outer.right - outer.left,
        outer.bottom - outer.top,
        nullptr,
        nullptr,
        instance,
        nullptr);
    if (!window) {
        writeLog("CreateWindowExW failed: %lu", GetLastError());
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return nullptr;
    }
    state->window = window;
    activeWindow = state;
    if (!setClientSize(window, config.width, config.height)) {
        writeLog("Failed to set the initial client size");
        DestroyWindow(window);
        activeWindow = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return nullptr;
    }
    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    rememberWindowedState(state);
    return window;
}

bool keyPressed(int virtualKey)
{
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

void updateGbaKeyState(
    mGBAHelper::KeyState* state,
    bool keyboardEnabled,
    const CSteam::ButtonState& steamState)
{
    state->up = (keyboardEnabled && keyPressed(VK_UP)) || steamState.up;
    state->down = (keyboardEnabled && keyPressed(VK_DOWN)) || steamState.down;
    state->left = (keyboardEnabled && keyPressed(VK_LEFT)) || steamState.left;
    state->right = (keyboardEnabled && keyPressed(VK_RIGHT)) || steamState.right;
    state->a = (keyboardEnabled && keyPressed('X')) || steamState.a;
    state->b = (keyboardEnabled && keyPressed('Z')) || steamState.b;
    state->l = (keyboardEnabled && keyPressed('A')) || steamState.l;
    state->r = (keyboardEnabled && keyPressed('S')) || steamState.r;
    state->start = (keyboardEnabled && keyPressed(VK_SPACE)) || steamState.start;
    state->select = (keyboardEnabled && keyPressed(VK_ESCAPE)) || steamState.select;
}

void printUsage(const char* executable)
{
    writeLog(
        "Usage: %s [-s <save.dat>] [-c <config.dat>] [-f <crt|no>] [rom.gba]",
        executable);
}
} // namespace

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int)
{
    enablePerMonitorDpiAwareness();

    // Keep startup diagnostics available when Steam is not running or SteamAPI_Init
    // fails. Once Steam initializes successfully, switch to the app install folder.
    const std::string launchDirectory = currentDirectory();
    selectLogPath(launchDirectory, true);
    writeLog("Launching %s %s for Windows", APP_NAME, APP_VERSION);

    ScopedLogger logger;
    std::string romPath;
    std::string sramPath = "save.dat";
    std::string configPath = "config.dat";
    bool usesDefaultSramPath = true;
    bool usesDefaultConfigPath = true;
    bool crtFilterEnabled = false;
    for (int i = 1; i < __argc; ++i) {
        const std::string argument = __argv[i];
        if (argument == "-s" || argument == "-c" || argument == "-f") {
            if (++i >= __argc) {
                printUsage(__argv[0]);
                return 1;
            }
            if (argument == "-s") {
                sramPath = __argv[i];
                usesDefaultSramPath = false;
            } else if (argument == "-c") {
                configPath = __argv[i];
                usesDefaultConfigPath = false;
            } else {
                const std::string filter = __argv[i];
                if (filter == "crt") {
                    crtFilterEnabled = true;
                } else if (filter == "no") {
                    crtFilterEnabled = false;
                } else {
                    writeLog("Unknown video filter: %s", filter.c_str());
                    printUsage(__argv[0]);
                    return 1;
                }
            }
        } else if (!romPath.empty()) {
            printUsage(__argv[0]);
            return 1;
        } else {
            romPath = argument;
        }
    }

    mGBAHelper gba;
    DewpointRuntime dewpoint(gba, [](const char* message) {
        writeLog("[Steam] %s", message);
    });
    std::string highScoreDirectory;
    if (!getHighScoreStorageDirectory(&highScoreDirectory) ||
        !dewpoint.setHighScoreStorageDirectory(highScoreDirectory)) {
        writeLog("Failed to prepare the pending high score directory: %s", highScoreDirectory.c_str());
    }
    const bool steamInitialized = dewpoint.initialize();
    if (steamInitialized) {
        std::string installDirectory;
        if (!getSteamInstallDirectory(&installDirectory)) {
            writeLog("Falling back to the current directory for logs and default save files: %s", launchDirectory.c_str());
            useCurrentDirectoryForDefaultPaths(
                launchDirectory,
                usesDefaultSramPath,
                usesDefaultConfigPath,
                &sramPath,
                &configPath);
        } else if (!selectLogPath(installDirectory, true)) {
            writeLog(
                "Failed to prepare the Steam log file; falling back to the current directory for logs and default save files: %s",
                launchDirectory.c_str());
            useCurrentDirectoryForDefaultPaths(
                launchDirectory,
                usesDefaultSramPath,
                usesDefaultConfigPath,
                &sramPath,
                &configPath);
        } else {
            writeLog("Launching %s %s for Windows", APP_NAME, APP_VERSION);
            if ((usesDefaultSramPath || usesDefaultConfigPath) &&
                !configureSteamSavePaths(
                    installDirectory,
                    usesDefaultSramPath,
                    usesDefaultConfigPath,
                    &sramPath,
                    &configPath)) {
                selectLogPath(launchDirectory, false);
                writeLog(
                    "Failed to prepare the Steam save directory; falling back to the current directory for logs and default save files: %s",
                    launchDirectory.c_str());
                useCurrentDirectoryForDefaultPaths(
                    launchDirectory,
                    usesDefaultSramPath,
                    usesDefaultConfigPath,
                    &sramPath,
                    &configPath);
            }
        }
    }

    std::vector<uint8_t> rom;
    const uint8_t* romData = game_rom;
    size_t romSize = game_rom_size;
    if (!romPath.empty()) {
        if (!readFile(romPath.c_str(), &rom)) {
            writeLog("Failed to read GBA ROM: %s", romPath.c_str());
            reportError("Failed to read the GBA ROM. See log.txt for details.");
            return 1;
        }
        romData = rom.data();
        romSize = rom.size();
    }

    gba.setSramPath(sramPath);
    if (!gba.load(romData, romSize)) {
        writeLog("Failed to load %s GBA ROM", romPath.empty() ? "embedded" : romPath.c_str());
        reportError("Failed to load the GBA ROM. See log.txt for details.");
        return 1;
    }

    CSteam steamInput;
    steamInput.setLoggger([](const char* message) {
        writeLog("[SteamInput] %s", message);
    });
    const bool steamInputInitialized = steamInitialized && steamInput.initializeInput();

    const WindowConfig config = loadWindowConfig(configPath);
    WindowState windowState{
        nullptr,
        nullptr,
        nullptr,
        &gba,
        true,
        false,
        CSteam::isEnabledWindowModo(),
        false,
        config.width,
        config.height,
        config.x,
        config.y,
    };
    HWND window = createApplicationWindow(instance, config, &windowState);
    if (!window) {
        reportError("Failed to create the application window. See log.txt for details.");
        return 1;
    }

    Direct3DRenderer renderer;
    DirectSoundOutput audio;
    windowState.renderer = &renderer;
    windowState.audio = &audio;
    if (!renderer.initialize(window, crtFilterEnabled) || !audio.initialize(window)) {
        reportError("Failed to initialize DirectX. See log.txt for details.");
        renderer.shutdown();
        audio.shutdown();
        DestroyWindow(window);
        activeWindow = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return 1;
    }

    if (!windowState.allowWindowed || config.fullscreen == -1) {
        setFullscreen(&windowState, true);
    }
    dewpoint.setFullscreenCallbacks(
        [&windowState](bool fullscreen) {
            return setFullscreen(&windowState, fullscreen);
        },
        [&windowState]() {
            return windowState.fullscreen;
        });

    int exitCode = 0;
    LARGE_INTEGER performanceFrequency{};
    LARGE_INTEGER previousPerformanceCounter{};
    if (!QueryPerformanceFrequency(&performanceFrequency) ||
        !QueryPerformanceCounter(&previousPerformanceCounter)) {
        writeLog("Failed to initialize the high-resolution performance counter");
        reportError("Failed to initialize the high-resolution timer. See log.txt for details.");
        audio.shutdown();
        renderer.shutdown();
        DestroyWindow(window);
        activeWindow = nullptr;
        UnregisterClassW(WINDOW_CLASS_NAME, instance);
        return 1;
    }
    const double emulationFrameTicks =
        static_cast<double>(performanceFrequency.QuadPart) / GBA_FRAME_RATE;
    // Present the nearest emulated frame at each VSync instead of immediately
    // repeating the first frame on displays whose refresh rate is just above 59.73 Hz.
    double emulationAccumulator = emulationFrameTicks * 1.5;
    while (windowState.running) {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) {
                windowState.running = false;
                break;
            }
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        if (!windowState.running) {
            break;
        }

        dewpoint.tick();
        if (steamInputInitialized) {
            steamInput.updateInput();
        }
        switch (steamInput.buttonState.type) {
            case CSteam::ControllerType::XBOX:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::Xbox);
                break;
            case CSteam::ControllerType::NintendoSwitch:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::NintendoSwitch);
                break;
            case CSteam::ControllerType::PlayStation:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::PlayStation);
                break;
            case CSteam::ControllerType::NotConnected:
                dewpoint.setGamepadType(DewpointRuntime::GamepadType::PCKeyboard);
                break;
        }
        const bool keyboardEnabled = GetForegroundWindow() == window && !IsIconic(window);
        updateGbaKeyState(&gba.keyState, keyboardEnabled, steamInput.buttonState);
        if (dewpoint.takeExitRequest(&exitCode)) {
            break;
        }
        if (windowState.paused) {
            QueryPerformanceCounter(&previousPerformanceCounter);
            Sleep(10);
            continue;
        }

        LARGE_INTEGER performanceCounter{};
        QueryPerformanceCounter(&performanceCounter);
        const double elapsedTicks =
            static_cast<double>(
                performanceCounter.QuadPart - previousPerformanceCounter.QuadPart);
        previousPerformanceCounter = performanceCounter;
        emulationAccumulator = std::min(
            emulationAccumulator + elapsedTicks,
            emulationFrameTicks * MAX_EMULATION_CATCH_UP_FRAMES);

        while (emulationAccumulator >= emulationFrameTicks) {
            gba.tick();
            emulationAccumulator -= emulationFrameTicks;
        }

        size_t soundSize = 0;
        uint16_t* sound = gba.dequeSound(&soundSize);
        if (soundSize > std::numeric_limits<DWORD>::max() ||
            !audio.queue(sound, static_cast<DWORD>(soundSize)) ||
            !renderer.render(gba.getVram())) {
            windowState.running = false;
            exitCode = 1;
        }
    }

    if (!saveWindowConfig(
            configPath,
            windowState.fullscreen,
            windowState.windowedWidth,
            windowState.windowedHeight,
            windowState.windowedX,
            windowState.windowedY)) {
        writeLog("Failed to save window configuration: %s", configPath.c_str());
    }
    if (!gba.saveSram()) {
        writeLog("Failed to save SRAM: %s", sramPath.c_str());
    }

    audio.shutdown();
    renderer.shutdown();
    if (IsWindow(window)) {
        DestroyWindow(window);
    }
    activeWindow = nullptr;
    UnregisterClassW(WINDOW_CLASS_NAME, instance);
    writeLog("Shutdown complete with exit code %d", exitCode);
    return exitCode;
}
