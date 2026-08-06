#include "keymap.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
using DewpointKeyMap::Binding;
using DewpointKeyMap::Button;
using DewpointKeyMap::Config;
using DewpointKeyMap::LoadResult;
using DewpointKeyMap::SpecialKey;

size_t index(Button button)
{
    return static_cast<size_t>(button);
}

const Binding& binding(const Config& config, Button button)
{
    return config.bindings[index(button)];
}

void writeFile(const std::filesystem::path& path, const std::string& contents)
{
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    assert(output);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    assert(output);
}

std::string readFile(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
}
} // namespace

int main()
{
    const Config defaults = DewpointKeyMap::defaultConfig();
    assert(binding(defaults, Button::Up).special == SpecialKey::Up);
    assert(binding(defaults, Button::A).character == 'x');
    assert(binding(defaults, Button::B).character == 'z');
    assert(binding(defaults, Button::L).character == 'a');
    assert(binding(defaults, Button::R).character == 's');
    assert(binding(defaults, Button::Start).special == SpecialKey::Space);
    assert(binding(defaults, Button::Select).special == SpecialKey::Escape);
    assert(!DewpointKeyMap::isAssigned(binding(defaults, Button::RapidA)));
    assert(!DewpointKeyMap::isAssigned(binding(defaults, Button::RapidB)));

    DewpointKeyMap::RapidFireState rapid{};
    for (int frame = 0; frame < 12; ++frame) {
        assert(DewpointKeyMap::advanceRapidFire(&rapid, true) == (frame % 6 < 3));
    }
    assert(!DewpointKeyMap::advanceRapidFire(&rapid, false));
    assert(DewpointKeyMap::advanceRapidFire(&rapid, true));

    DewpointKeyMap::RapidFireState oneSecond{};
    bool previouslyPressed = false;
    int pressCount = 0;
    for (int frame = 0; frame < 60; ++frame) {
        const bool pressed = DewpointKeyMap::advanceRapidFire(&oneSecond, true);
        if (pressed && !previouslyPressed) {
            ++pressCount;
        }
        previouslyPressed = pressed;
    }
    assert(pressCount == 10);

    const std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("dewpoint-keymap-test-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    const std::filesystem::path path = directory / "keymap.ini";

    Config config{};
    std::vector<std::string> diagnostics;
    std::string error;
    assert(DewpointKeyMap::load(path.string(), &config, &diagnostics, &error) == LoadResult::Missing);
    assert(diagnostics.empty());
    assert(error.empty());
    assert(DewpointKeyMap::writeDefault(path.string(), &error));
    assert(error.empty());
    assert(readFile(path) ==
        "; Keyboard mappings use BUTTON = KEY. Button names and key names are case-insensitive.\n"
        "; Buttons: UP, DOWN, LEFT, RIGHT, A, B, L, R, START, SELECT, RAPID_A, RAPID_B.\n"
        "; RAPID_A and RAPID_B are optional and alternate down/up every three frames\n"
        "; (10 presses per second at 60 frames per second).\n"
        "; Keys: A-Z, an unmodified ASCII punctuation character, up/down/left/right,\n"
        "; enter/return, esc/escape, tab, spc/space, lshift, or rshift.\n"
        "; Number keys, function keys, and characters requiring modifiers are not supported.\n"
        "; Values follow the current keyboard layout; they do not identify physical key positions.\n"
        "; After trimming spaces and tabs, lines beginning with a semicolon are comments.\n"
        "; A semicolon remains a valid value, for example: B = ;\n"
        "\n"
        "UP = Up\n"
        "DOWN = Down\n"
        "LEFT = Left\n"
        "RIGHT = Right\n"
        "A = X\n"
        "B = Z\n"
        "L = A\n"
        "R = S\n"
        "START = Space\n"
        "SELECT = Escape\n");

    writeFile(
        path,
        "\xEF\xBB\xBFup=RETURN\r\n"
        "Down = esc\r\n"
        "left = TAB\r\n"
        "RIGHT = spc\r\n"
        "a = q\r\n"
        "B = ;\r\n"
        "L = #\r\n"
        "R ==\r\n"
        "start = lshift\r\n"
        "select = RSHIFT\r\n"
        "rapid_a = O\r\n"
        "RAPID_B = P\r\n");
    assert(DewpointKeyMap::load(path.string(), &config, &diagnostics, &error) == LoadResult::Loaded);
    assert(diagnostics.empty());
    assert(binding(config, Button::Up).special == SpecialKey::Enter);
    assert(binding(config, Button::Down).special == SpecialKey::Escape);
    assert(binding(config, Button::Left).special == SpecialKey::Tab);
    assert(binding(config, Button::Right).special == SpecialKey::Space);
    assert(binding(config, Button::A).character == 'q');
    assert(binding(config, Button::B).character == ';');
    assert(binding(config, Button::L).character == '#');
    assert(binding(config, Button::R).character == '=');
    assert(binding(config, Button::Start).special == SpecialKey::LeftShift);
    assert(binding(config, Button::Select).special == SpecialKey::RightShift);
    assert(binding(config, Button::RapidA).character == 'o');
    assert(binding(config, Button::RapidB).character == 'p');

    writeFile(
        path,
        " \t; a full-line comment after spaces and tabs\n"
        "# another full-line comment\n"
        "A = O\n"
        "A = Q\n"
        "B = 1\n"
        "L = F1\n"
        "R = é\n"
        "UNKNOWN = X\n"
        "not an assignment\n");
    assert(DewpointKeyMap::load(path.string(), &config, &diagnostics, &error) == LoadResult::Loaded);
    assert(diagnostics.size() == 7);
    assert(binding(config, Button::A).character == 'q');
    assert(binding(config, Button::B).character == 'z');
    assert(binding(config, Button::L).character == 'a');
    assert(binding(config, Button::R).character == 's');

    assert(DewpointKeyMap::load(directory.string(), &config, &diagnostics, &error) == LoadResult::Unreadable);
    assert(!error.empty());
    assert(!DewpointKeyMap::writeDefault((directory / "missing" / "keymap.ini").string(), &error));
    assert(!error.empty());

    std::filesystem::remove_all(directory);
    return 0;
}
