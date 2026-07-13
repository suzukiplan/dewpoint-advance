#include "mgbahelper.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include <mgba/core/log.h>

namespace
{
constexpr int FRAME_COUNT = 600;

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

void writeU16(std::ostream& output, uint16_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
    };
    output.write(bytes, sizeof(bytes));
}

void writeU32(std::ostream& output, uint32_t value)
{
    const char bytes[] = {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff),
    };
    output.write(bytes, sizeof(bytes));
}

bool writeBitmap(const std::string& path, const uint32_t* pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0) {
        return false;
    }

    constexpr uint32_t FILE_HEADER_SIZE = 14;
    constexpr uint32_t INFO_HEADER_SIZE = 40;
    const uint32_t rowSize = (static_cast<uint32_t>(width) * 3 + 3) & ~uint32_t(3);
    const uint32_t pixelDataSize = rowSize * static_cast<uint32_t>(height);
    const uint32_t pixelOffset = FILE_HEADER_SIZE + INFO_HEADER_SIZE;

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return false;
    }

    output.put('B');
    output.put('M');
    writeU32(output, pixelOffset + pixelDataSize);
    writeU16(output, 0);
    writeU16(output, 0);
    writeU32(output, pixelOffset);

    writeU32(output, INFO_HEADER_SIZE);
    writeU32(output, static_cast<uint32_t>(width));
    writeU32(output, static_cast<uint32_t>(height));
    writeU16(output, 1);
    writeU16(output, 24);
    writeU32(output, 0);
    writeU32(output, pixelDataSize);
    writeU32(output, 2835);
    writeU32(output, 2835);
    writeU32(output, 0);
    writeU32(output, 0);

    const char padding[3] = {};
    const size_t paddingSize = rowSize - static_cast<uint32_t>(width) * 3;
    for (int y = height - 1; y >= 0; --y) {
        for (int x = 0; x < width; ++x) {
            const uint32_t color = pixels[y * width + x];
            const char bgr[] = {
                static_cast<char>((color >> 16) & 0xff),
                static_cast<char>((color >> 8) & 0xff),
                static_cast<char>(color & 0xff),
            };
            output.write(bgr, sizeof(bgr));
        }
        output.write(padding, paddingSize);
    }
    return output.good();
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

    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <rom.gba>\n";
        return 1;
    }

    std::vector<uint8_t> rom;
    if (!readFile(argv[1], &rom)) {
        std::cerr << "Failed to read GBA ROM: " << argv[1] << '\n';
        return 1;
    }

    mGBAHelper gba;
    if (!gba.load(rom.data(), rom.size())) {
        std::cerr << "Failed to load GBA ROM: " << argv[1] << '\n';
        return 1;
    }

    for (int frame = 0; frame < FRAME_COUNT; ++frame) {
        gba.tick();
    }

    if (!writeBitmap("vram.bmp", gba.getVram(), gba.getVramWidth(), gba.getVramHeight())) {
        std::cerr << "Failed to write vram.bmp\n";
        return 1;
    }

    std::cout << "Executed " << FRAME_COUNT << " frames and wrote vram.bmp\n";
    return 0;
}
