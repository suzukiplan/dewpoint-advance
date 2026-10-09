#include "runtime_config.h"
#include <cassert>
#include <filesystem>
#include <vector>
#include <unistd.h>

int main()
{
    char directory[] = "/tmp/dpa-config-XXXXXX";
    assert(mkdtemp(directory));
    const std::string path = std::string(directory) + "/config.dat";
    DewpointConfig::Config config{-1, 960, 640, -100, 42};
    assert(config.dmg_vol == 100 && config.pcm_vol == 100);
    bool legacy = true;
    DewpointConfig::Config loaded{};
    assert(!DewpointConfig::load(path, &loaded, &legacy));
    config.dmg_vol = 0;
    config.pcm_vol = 37;
    assert(DewpointConfig::save(path, config));
    assert(DewpointConfig::load(path, &loaded, &legacy) && !legacy);
    assert(loaded.x == -100 && loaded.fullscreen == -1);
    assert(loaded.dmg_vol == 0 && loaded.pcm_vol == 37);
    std::ifstream input(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    input.close();
    assert(bytes.size() == 36 && std::memcmp(bytes.data(), "DPAC\0\1\44\0", 8) == 0);
    auto write = [&](const std::vector<char>& data) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(data.data(), data.size());
    };
    write(std::vector<char>(bytes.begin() + 8, bytes.begin() + 28));
    assert(DewpointConfig::load(path, &loaded, &legacy) && legacy);
    assert(loaded.width == 960 && loaded.y == 42 && loaded.x == -100);
    assert(loaded.dmg_vol == 100 && loaded.pcm_vol == 100);
    assert(DewpointConfig::save(path, loaded));
    assert(DewpointConfig::load(path, &loaded, &legacy) && !legacy);
    for (size_t offset : {0u, 4u, 5u, 6u, 7u, 8u, 28u, 32u}) {
        auto invalid = bytes;
        invalid[offset] = static_cast<char>(127);
        write(invalid);
        assert(!DewpointConfig::load(path, &loaded, &legacy));
    }
    for (size_t length : {0u, 4u, 19u, 21u, 35u, 37u}) {
        auto invalid = bytes;
        invalid.resize(length);
        write(invalid);
        assert(!DewpointConfig::load(path, &loaded, &legacy));
    }
    config.dmg_vol = 101;
    assert(!DewpointConfig::save(path, config));
    config.dmg_vol = -1;
    assert(!DewpointConfig::save(path, config));
    config.dmg_vol = 100;
    assert(!DewpointConfig::save(path + "/missing", config));
    assert(DewpointConfig::save(path, config));
    // A failed replacement must preserve the destination and clean up its temporary file.
    std::filesystem::create_directory(path + ".tmp");
    config.pcm_vol = 0;
    assert(!DewpointConfig::save(path + ".tmp", config));
    assert(std::filesystem::is_directory(path + ".tmp"));
    assert(DewpointConfig::load(path, &loaded, &legacy));
    assert(loaded.pcm_vol == 37);
    assert(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator{}) == 2);
    std::filesystem::remove_all(directory);
}
