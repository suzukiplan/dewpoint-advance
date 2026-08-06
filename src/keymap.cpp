/**
 * Dewpoint Advance Keyboard Map
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#include "keymap.h"

#include "pathutil.h"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace
{
using DewpointKeyMap::Binding;
using DewpointKeyMap::Button;
using DewpointKeyMap::SpecialKey;

constexpr size_t MAX_KEYMAP_SIZE = 64 * 1024;
constexpr unsigned RAPID_FIRE_PHASE_FRAMES = 3;
constexpr unsigned RAPID_FIRE_CYCLE_FRAMES = RAPID_FIRE_PHASE_FRAMES * 2;

constexpr const char* KEYMAP_GUIDE =
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
    "\n";

size_t buttonIndex(Button button)
{
    return static_cast<size_t>(button);
}

std::string trim(const std::string& value)
{
    const size_t first = value.find_first_not_of(" \t");
    if (first == std::string::npos) {
        return {};
    }
    const size_t last = value.find_last_not_of(" \t");
    return value.substr(first, last - first + 1);
}

std::string lowerAscii(std::string value)
{
    for (char& c : value) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return value;
}

bool parseButton(const std::string& value, Button* button)
{
    const std::string name = lowerAscii(value);
    if (name == "up") {
        *button = Button::Up;
    } else if (name == "down") {
        *button = Button::Down;
    } else if (name == "left") {
        *button = Button::Left;
    } else if (name == "right") {
        *button = Button::Right;
    } else if (name == "a") {
        *button = Button::A;
    } else if (name == "b") {
        *button = Button::B;
    } else if (name == "l") {
        *button = Button::L;
    } else if (name == "r") {
        *button = Button::R;
    } else if (name == "start") {
        *button = Button::Start;
    } else if (name == "select") {
        *button = Button::Select;
    } else if (name == "rapid_a") {
        *button = Button::RapidA;
    } else if (name == "rapid_b") {
        *button = Button::RapidB;
    } else {
        return false;
    }
    return true;
}

bool parseSpecialKey(const std::string& value, SpecialKey* special)
{
    if (value == "up") {
        *special = SpecialKey::Up;
    } else if (value == "down") {
        *special = SpecialKey::Down;
    } else if (value == "left") {
        *special = SpecialKey::Left;
    } else if (value == "right") {
        *special = SpecialKey::Right;
    } else if (value == "enter" || value == "return") {
        *special = SpecialKey::Enter;
    } else if (value == "esc" || value == "escape") {
        *special = SpecialKey::Escape;
    } else if (value == "tab") {
        *special = SpecialKey::Tab;
    } else if (value == "spc" || value == "space") {
        *special = SpecialKey::Space;
    } else if (value == "lshift") {
        *special = SpecialKey::LeftShift;
    } else if (value == "rshift") {
        *special = SpecialKey::RightShift;
    } else {
        return false;
    }
    return true;
}

bool parseBinding(const std::string& value, Binding* binding)
{
    const std::string normalized = lowerAscii(value);
    SpecialKey special = SpecialKey::None;
    if (parseSpecialKey(normalized, &special)) {
        *binding = Binding{0, special};
        return true;
    }
    if (normalized.size() != 1) {
        return false;
    }

    const unsigned char character = static_cast<unsigned char>(normalized[0]);
    const bool letter = character >= 'a' && character <= 'z';
    const bool punctuation =
        (character >= '!' && character <= '/') ||
        (character >= ':' && character <= '@') ||
        (character >= '[' && character <= '`') ||
        (character >= '{' && character <= '~');
    if (!letter && !punctuation) {
        return false;
    }
    *binding = Binding{static_cast<char>(character), SpecialKey::None};
    return true;
}

const char* specialKeyName(SpecialKey special)
{
    switch (special) {
        case SpecialKey::Up: return "Up";
        case SpecialKey::Down: return "Down";
        case SpecialKey::Left: return "Left";
        case SpecialKey::Right: return "Right";
        case SpecialKey::Enter: return "Enter";
        case SpecialKey::Escape: return "Escape";
        case SpecialKey::Tab: return "Tab";
        case SpecialKey::Space: return "Space";
        case SpecialKey::LeftShift: return "LShift";
        case SpecialKey::RightShift: return "RShift";
        case SpecialKey::None: break;
    }
    return "Unknown";
}

void addDiagnostic(
    std::vector<std::string>* diagnostics,
    size_t lineNumber,
    const std::string& message)
{
    if (diagnostics) {
        diagnostics->push_back("line " + std::to_string(lineNumber) + ": " + message);
    }
}
} // namespace

namespace DewpointKeyMap
{
Config defaultConfig()
{
    return Config{{
        Binding{0, SpecialKey::Up},
        Binding{0, SpecialKey::Down},
        Binding{0, SpecialKey::Left},
        Binding{0, SpecialKey::Right},
        Binding{'x', SpecialKey::None},
        Binding{'z', SpecialKey::None},
        Binding{'a', SpecialKey::None},
        Binding{'s', SpecialKey::None},
        Binding{0, SpecialKey::Space},
        Binding{0, SpecialKey::Escape},
        Binding{0, SpecialKey::None},
        Binding{0, SpecialKey::None},
    }};
}

const char* buttonName(Button button)
{
    switch (button) {
        case Button::Up: return "UP";
        case Button::Down: return "DOWN";
        case Button::Left: return "LEFT";
        case Button::Right: return "RIGHT";
        case Button::A: return "A";
        case Button::B: return "B";
        case Button::L: return "L";
        case Button::R: return "R";
        case Button::Start: return "START";
        case Button::Select: return "SELECT";
        case Button::RapidA: return "RAPID_A";
        case Button::RapidB: return "RAPID_B";
        case Button::Count: break;
    }
    return "UNKNOWN";
}

std::string bindingName(const Binding& binding)
{
    if (!isAssigned(binding)) {
        return "Unassigned";
    }
    if (binding.special != SpecialKey::None) {
        return specialKeyName(binding.special);
    }
    if (binding.character >= 'a' && binding.character <= 'z') {
        return std::string(1, static_cast<char>(binding.character - 'a' + 'A'));
    }
    return std::string(1, binding.character);
}

bool isAssigned(const Binding& binding)
{
    return binding.character != 0 || binding.special != SpecialKey::None;
}

char buttonCharacter(const Binding& binding)
{
    if (binding.special != SpecialKey::None || binding.character == 0) {
        return '?';
    }
    if (binding.character >= 'a' && binding.character <= 'z') {
        return static_cast<char>(binding.character - 'a' + 'A');
    }
    return binding.character;
}

char buttonCharacter(const Config& config, Button button)
{
    const size_t index = static_cast<size_t>(button);
    if (index >= config.bindings.size()) {
        return '?';
    }
    return buttonCharacter(config.bindings[index]);
}

bool advanceRapidFire(RapidFireState* state, bool held)
{
    if (!state) {
        return false;
    }
    if (!held) {
        state->phase = 0;
        return false;
    }

    const bool pressed = state->phase < RAPID_FIRE_PHASE_FRAMES;
    state->phase = (state->phase + 1) % RAPID_FIRE_CYCLE_FRAMES;
    return pressed;
}

LoadResult load(
    const std::string& path,
    Config* config,
    std::vector<std::string>* diagnostics,
    std::string* errorMessage)
{
    if (!config) {
        if (errorMessage) {
            *errorMessage = "missing output configuration";
        }
        return LoadResult::Unreadable;
    }
    *config = defaultConfig();
    if (diagnostics) {
        diagnostics->clear();
    }
    if (errorMessage) {
        errorMessage->clear();
    }

    bool exists = false;
    bool isDirectory = false;
    std::string inspectError;
    if (!DewpointPath::inspect(path, &exists, &isDirectory, &inspectError)) {
        if (errorMessage) {
            *errorMessage = inspectError.empty() ? "could not inspect file" : inspectError;
        }
        return LoadResult::Unreadable;
    }
    if (!exists) {
        return LoadResult::Missing;
    }
    if (isDirectory) {
        if (errorMessage) {
            *errorMessage = "path is a directory";
        }
        return LoadResult::Unreadable;
    }

    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        if (errorMessage) {
            *errorMessage = "could not open file";
        }
        return LoadResult::Unreadable;
    }

    const std::streamsize size = input.tellg();
    if (size < 0 || static_cast<uintmax_t>(size) > MAX_KEYMAP_SIZE) {
        if (errorMessage) {
            *errorMessage = "file is larger than 64 KiB";
        }
        return LoadResult::Unreadable;
    }
    input.seekg(0);

    Config parsed = defaultConfig();
    std::array<bool, BUTTON_COUNT> assigned{};
    std::string line;
    size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (lineNumber == 1 && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
            line.erase(0, 3);
        }
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        const std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';') {
            continue;
        }
        const size_t separator = trimmed.find('=');
        if (separator == std::string::npos) {
            addDiagnostic(diagnostics, lineNumber, "expected key = value");
            continue;
        }

        const std::string key = trim(trimmed.substr(0, separator));
        const std::string value = trim(trimmed.substr(separator + 1));
        Button button = Button::Count;
        if (!parseButton(key, &button)) {
            addDiagnostic(diagnostics, lineNumber, "unknown button: " + key);
            continue;
        }
        Binding binding{};
        if (!parseBinding(value, &binding)) {
            addDiagnostic(diagnostics, lineNumber, "invalid key value for " + key + ": " + value);
            continue;
        }

        const size_t index = buttonIndex(button);
        if (assigned[index]) {
            addDiagnostic(diagnostics, lineNumber, "duplicate assignment for " + std::string(buttonName(button)));
        }
        parsed.bindings[index] = binding;
        assigned[index] = true;
    }
    if (input.bad()) {
        if (errorMessage) {
            *errorMessage = "failed while reading file";
        }
        return LoadResult::Unreadable;
    }

    *config = parsed;
    return LoadResult::Loaded;
}

bool writeDefault(const std::string& path, std::string* errorMessage)
{
    if (errorMessage) {
        errorMessage->clear();
    }
    errno = 0;
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (errorMessage) {
            *errorMessage = errno ? std::strerror(errno) : "could not create file";
        }
        return false;
    }
    output << KEYMAP_GUIDE;
    const Config defaults = defaultConfig();
    for (size_t index = 0; index < BUTTON_COUNT; ++index) {
        if (!isAssigned(defaults.bindings[index])) {
            continue;
        }
        const auto button = static_cast<Button>(index);
        output << buttonName(button) << " = " << bindingName(defaults.bindings[index]) << '\n';
    }
    output.flush();
    if (!output) {
        if (errorMessage) {
            *errorMessage = errno ? std::strerror(errno) : "failed to write file";
        }
        return false;
    }
    return true;
}
} // namespace DewpointKeyMap
