#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <string>

namespace
{

namespace fs = std::filesystem;

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

bool readRomPath(const fs::path& configPath, fs::path& romPath)
{
    std::ifstream config(configPath);
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

        romPath = fs::path(value);
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

    if (romPath.is_relative()) {
        romPath = configPath.parent_path() / romPath;
    }
    return true;
}

bool shouldSkipGeneration(const fs::path& romPath, const fs::path& sourcePath, bool& skip)
{
    skip = false;

    std::error_code error;
    const bool romExists = fs::exists(romPath, error);
    if (error) {
        std::cerr << "makerom: cannot check ROM file: " << romPath << ": " << error.message() << '\n';
        return false;
    }
    if (!romExists) {
        return true;
    }
    const bool romIsFile = fs::is_regular_file(romPath, error);
    if (error) {
        std::cerr << "makerom: cannot check ROM file type: " << romPath << ": " << error.message() << '\n';
        return false;
    }
    if (!romIsFile) {
        return true;
    }

    const bool sourceExists = fs::exists(sourcePath, error);
    if (error) {
        std::cerr << "makerom: cannot check output file: " << sourcePath << ": " << error.message() << '\n';
        return false;
    }
    if (!sourceExists) {
        return true;
    }
    const bool sourceIsFile = fs::is_regular_file(sourcePath, error);
    if (error) {
        std::cerr << "makerom: cannot check output file type: " << sourcePath << ": " << error.message() << '\n';
        return false;
    }
    if (!sourceIsFile) {
        return true;
    }

    const fs::file_time_type romTime = fs::last_write_time(romPath, error);
    if (error) {
        std::cerr << "makerom: cannot read ROM file modification time: " << romPath << ": " << error.message() << '\n';
        return false;
    }
    const fs::file_time_type sourceTime = fs::last_write_time(sourcePath, error);
    if (error) {
        std::cerr << "makerom: cannot read output file modification time: " << sourcePath << ": " << error.message() << '\n';
        return false;
    }

    skip = sourceTime > romTime;
    return true;
}

bool writeSource(const fs::path& romPath, const fs::path& sourcePath)
{
    std::error_code error;
    const std::uintmax_t romSize = fs::file_size(romPath, error);
    if (error) {
        std::cerr << "makerom: cannot determine ROM file size: " << romPath << ": " << error.message() << '\n';
        return false;
    }
    if (romSize > std::numeric_limits<std::size_t>::max()) {
        std::cerr << "makerom: ROM file is too large: " << romPath << '\n';
        return false;
    }

    std::ifstream rom(romPath, std::ios::binary);
    if (!rom) {
        std::cerr << "makerom: cannot open ROM file: " << romPath << '\n';
        return false;
    }

    std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
    if (!source) {
        std::cerr << "makerom: cannot open output file: " << sourcePath << '\n';
        return false;
    }

    source << "#include <vgs.h>\n\n";
    source << "const uint8_t game_rom[" << romSize << "] = {\n";

    std::uint8_t buffer[16];
    bool firstLine = true;
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
            source << "0x" << std::uppercase << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned int>(buffer[i]);
        }
    }
    if (!rom.eof()) {
        std::cerr << "makerom: failed while reading ROM file: " << romPath << '\n';
        return false;
    }

    source << "\n};\n";
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

    const fs::path configPath(argv[1]);
    const fs::path sourcePath(argv[2]);
    fs::path romPath;
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
