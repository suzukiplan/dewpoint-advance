// Shared, explicitly little-endian config.dat format. MIT License.
#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>

namespace DewpointConfig
{
struct Config {
    int32_t fullscreen;
    int32_t width;
    int32_t height;
    int32_t x;
    int32_t y;
    int32_t dmg_vol = 100;
    int32_t pcm_vol = 100;
};

inline bool valid(const Config& c)
{
    return (c.fullscreen == -1 || c.fullscreen == 0) && c.width > 0 && c.height > 0 &&
           c.dmg_vol >= 0 && c.dmg_vol <= 100 && c.pcm_vol >= 0 && c.pcm_vol <= 100;
}

inline bool save(const std::string& path, const Config& c)
{
    if (!valid(c)) return false;
    std::array<uint8_t, 36> bytes{{'D', 'P', 'A', 'C', 0, 1, 36, 0}};
    const int32_t fields[] = {c.fullscreen, c.width, c.height, c.x, c.y, c.dmg_vol, c.pcm_vol};
    for (size_t i = 0; i < 7; ++i) {
        const uint32_t value = static_cast<uint32_t>(fields[i]);
        for (size_t j = 0; j < 4; ++j) bytes[8 + i * 4 + j] = value >> (j * 8);
    }
    // Preserve the previous configuration if writing fails. Exclusive creation
    // prevents concurrent writers from sharing the same temporary file.
    static std::atomic<uint64_t> sequence{0};
    const std::string temporary = path + ".tmp." +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." +
        std::to_string(sequence.fetch_add(1));
    FILE* output = std::fopen(temporary.c_str(), "wbx");
    if (!output) return false;
    const bool wrote = std::fwrite(bytes.data(), 1, bytes.size(), output) == bytes.size();
    const bool closed = std::fclose(output) == 0;
    bool replaced = false;
    if (wrote && closed) {
#ifdef _WIN32
        replaced = MoveFileExA(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
        replaced = std::rename(temporary.c_str(), path.c_str()) == 0;
#endif
    }
    if (!replaced) std::remove(temporary.c_str());
    return replaced;
}

// A legacy file is accepted only at its exact original size.
inline bool load(const std::string& path, Config* result, bool* legacy)
{
    *legacy = false;
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return false;
    const auto size = input.tellg();
    if (size != 20 && size != 36) return false;
    std::array<uint8_t, 36> bytes{};
    input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(bytes.data()), size)) return false;
    const bool header = std::memcmp(bytes.data(), "DPAC", 4) == 0;
    if (header) {
        if (size != 36 || bytes[4] != 0 || bytes[5] != 1 || bytes[6] != 36 || bytes[7] != 0) return false;
    } else if (size != 20) {
        return false;
    }
    Config c{};
    int32_t* fields[] = {&c.fullscreen, &c.width, &c.height, &c.x, &c.y, &c.dmg_vol, &c.pcm_vol};
    for (size_t i = 0; i < (header ? 7u : 5u); ++i) {
        uint32_t value = 0;
        for (size_t j = 0; j < 4; ++j) value |= uint32_t(bytes[(header ? 8 : 0) + i * 4 + j]) << (j * 8);
        std::memcpy(fields[i], &value, sizeof(value));
    }
    if (!valid(c)) return false;
    *result = c;
    *legacy = !header;
    return true;
}
}
