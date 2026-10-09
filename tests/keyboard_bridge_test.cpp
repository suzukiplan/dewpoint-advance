#include "dewpoint_runtime.h"
#include "keymap.h"
#include "../sdk/dpa.h"
#include <cassert>
#include <filesystem>
#include <chrono>

int main()
{
    mGBAHelper gba;
    DewpointRuntime runtime(gba);
    constexpr uint32_t input = 25, button = 26, set = 27, get = 28;
    assert(runtime.readRegister(input) == 0);
    assert(runtime.readRegister(get) == UINT32_MAX);
    runtime.writeRegister(button, DbaButtonIdA);
    runtime.writeRegister(set, 'Q');
    assert(runtime.readRegister(set) == UINT32_MAX);
    const auto directory = std::filesystem::temp_directory_path() /
        ("dpa-keyboard-bridge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const auto path = (directory / "keymap.ini").string();
    auto config = DewpointKeyMap::defaultConfig();
    int held = 0, calls = 0;
    bool fail = false;
    runtime.setKeyboardCallbacks([&]() { return held; }, [&](int id, int code) {
        ++calls;
        std::string error;
        return !fail && DewpointKeyMap::set(path, &config, id, code, &error) ? 0 : -1;
    }, [&](int id) { return DewpointKeyMap::get(config, id); });
    assert(runtime.readRegister(get) == 'X');
    held = DpaKeyLeftShift;
    assert(runtime.readRegister(input) == DpaKeyLeftShift);
    held = 0;
    assert(runtime.readRegister(input) == 0);
    runtime.writeRegister(set, 'Q');
    assert(runtime.readRegister(set) == 0 && runtime.readRegister(get) == 'Q');
    fail = true;
    runtime.writeRegister(set, 'W');
    assert(runtime.readRegister(set) == UINT32_MAX && runtime.readRegister(get) == 'Q');
    fail = false;
    runtime.writeRegister(set, '1');
    assert(runtime.readRegister(set) == UINT32_MAX && runtime.readRegister(get) == 'Q');
    runtime.writeRegister(set, UINT32_MAX);
    assert(runtime.readRegister(set) == UINT32_MAX && calls == 3);
    for (uint32_t id : {12u, UINT32_MAX}) {
        runtime.writeRegister(button, id);
        runtime.writeRegister(set, 'W');
        assert(runtime.readRegister(get) == UINT32_MAX);
        assert(runtime.readRegister(set) == UINT32_MAX && calls == 3);
    }
    runtime.reset();
    assert(runtime.readRegister(get) == UINT32_MAX);
    runtime.writeRegister(button, DbaButtonIdA);
    assert(runtime.readRegister(get) == 'Q');
    runtime.writeRegister(set, 0);
    assert(runtime.readRegister(set) == 0 && runtime.readRegister(get) == 0);
    DewpointKeyMap::Config loaded{};
    assert(DewpointKeyMap::load(path, &loaded, nullptr, nullptr) == DewpointKeyMap::LoadResult::Loaded);
    assert(DewpointKeyMap::get(loaded, DbaButtonIdA) == 0);
    std::filesystem::remove_all(directory);
}
