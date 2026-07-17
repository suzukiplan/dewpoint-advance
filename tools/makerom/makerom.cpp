#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>
#include <sys/stat.h>

namespace
{

constexpr std::uintmax_t NINTENDO_LOGO_OFFSET = 0x004;
constexpr std::uintmax_t NINTENDO_LOGO_SIZE = 156;

void putUsage()
{
    std::cerr << "usage: makerom /path/to/package.conf /path/to/source.c\n";
}

std::string trim(const std::string& value)
{
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }

    std::size_t last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

bool isAbsolutePath(const std::string& path)
{
    if (path.empty()) {
        return false;
    }
    if (path[0] == '/' || path[0] == '\\') {
        return true;
    }
    return path.size() >= 2 && std::isalpha(static_cast<unsigned char>(path[0])) && path[1] == ':';
}

std::string resolvePath(const std::string& configPath, const std::string& path)
{
    if (isAbsolutePath(path)) {
        return path;
    }
    const std::size_t separator = configPath.find_last_of("/\\");
    if (separator == std::string::npos) {
        return path;
    }
    return configPath.substr(0, separator + 1) + path;
}

bool readRomPath(const std::string& configPath, std::string& romPath)
{
    std::ifstream config(configPath.c_str());
    if (!config) {
        std::cerr << "makerom: cannot open package configuration: " << configPath << '\n';
        return false;
    }

    std::string line;
    std::size_t lineNumber = 0;
    bool found = false;
    while (std::getline(config, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) {
            line.erase(0, 3);
        }

        const std::string stripped = trim(line);
        if (stripped.empty() || stripped[0] == '#' || stripped[0] == ';') {
            continue;
        }

        const std::size_t separator = stripped.find('=');
        if (separator == std::string::npos || trim(stripped.substr(0, separator)) != "RomFile") {
            continue;
        }

        const std::string value = trim(stripped.substr(separator + 1));
        if (value.empty()) {
            std::cerr << "makerom: RomFile is empty at " << configPath << ':' << lineNumber << '\n';
            return false;
        }
        if (found) {
            std::cerr << "makerom: RomFile is defined more than once in " << configPath << '\n';
            return false;
        }

        romPath = value;
        found = true;
    }

    if (config.bad()) {
        std::cerr << "makerom: failed while reading package configuration: " << configPath << '\n';
        return false;
    }
    if (!found) {
        std::cerr << "makerom: RomFile is not defined in " << configPath << '\n';
        return false;
    }

    romPath = resolvePath(configPath, romPath);
    return true;
}

bool shouldSkipGeneration(const std::string& romPath, const std::string& sourcePath, bool& skip)
{
    skip = false;

    struct stat romStatus{};
    if (stat(romPath.c_str(), &romStatus) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        std::cerr << "makerom: cannot check ROM file: " << romPath << ": " << std::strerror(errno) << '\n';
        return false;
    }
    if (!S_ISREG(romStatus.st_mode)) {
        return true;
    }

    struct stat sourceStatus{};
    if (stat(sourcePath.c_str(), &sourceStatus) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        std::cerr << "makerom: cannot check output file: " << sourcePath << ": " << std::strerror(errno) << '\n';
        return false;
    }
    if (!S_ISREG(sourceStatus.st_mode)) {
        return true;
    }

    skip = sourceStatus.st_mtime > romStatus.st_mtime;
    return true;
}

bool writeSource(const std::string& romPath, const std::string& sourcePath)
{
    std::ifstream rom(romPath.c_str(), std::ios::binary | std::ios::ate);
    if (!rom) {
        std::cerr << "makerom: cannot open ROM file: " << romPath << '\n';
        return false;
    }
    const std::streamoff end = rom.tellg();
    if (end < 0 || static_cast<std::uintmax_t>(end) > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "makerom: cannot determine ROM file size: " << romPath << '\n';
        return false;
    }
    const std::uintmax_t romSize = static_cast<std::uintmax_t>(end);
    rom.seekg(0);
    if (!rom) {
        std::cerr << "makerom: cannot seek ROM file: " << romPath << '\n';
        return false;
    }

    std::ofstream source(sourcePath.c_str(), std::ios::binary | std::ios::trunc);
    if (!source) {
        std::cerr << "makerom: cannot open output file: " << sourcePath << '\n';
        return false;
    }

    source << "#include <stddef.h>\n";
    source << "#include <stdint.h>\n\n";
    source << "const uint8_t game_rom[" << romSize << "] = {\n";

    std::uint8_t buffer[16];
    bool firstLine = true;
    std::uintmax_t offset = 0;
    while (rom.read(reinterpret_cast<char*>(buffer), sizeof(buffer)) || rom.gcount() > 0) {
        const std::streamsize count = rom.gcount();
        if (!firstLine) {
            source << ",\n";
        }
        firstLine = false;
        source << "    ";
        for (std::streamsize i = 0; i < count; ++i) {
            if (i != 0) {
                source << ", ";
            }
            const std::uintmax_t byteOffset = offset + static_cast<std::uintmax_t>(i);
            const bool isNintendoLogo = NINTENDO_LOGO_OFFSET <= byteOffset &&
                                        byteOffset < NINTENDO_LOGO_OFFSET + NINTENDO_LOGO_SIZE;
            const unsigned int value = isNintendoLogo ? 0 : buffer[i];
            source << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << value;
        }
        offset += static_cast<std::uintmax_t>(count);
    }
    if (!rom.eof()) {
        std::cerr << "makerom: failed while reading ROM file: " << romPath << '\n';
        return false;
    }

    source << "\n};\n\n";
    source << "const size_t game_rom_size = sizeof(game_rom);\n";
    if (!source) {
        std::cerr << "makerom: failed while writing output file: " << sourcePath << '\n';
        return false;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc != 3) {
        putUsage();
        return 1;
    }

    const std::string configPath(argv[1]);
    const std::string sourcePath(argv[2]);
    std::string romPath;
    if (!readRomPath(configPath, romPath)) {
        return 1;
    }

    bool skip = false;
    if (!shouldSkipGeneration(romPath, sourcePath, skip)) {
        return 1;
    }
    if (skip) {
        puts("Skipped.");
        return 0;
    }
    if (!writeSource(romPath, sourcePath)) {
        return 1;
    }
    puts("Generated.");
    return 0;
}
