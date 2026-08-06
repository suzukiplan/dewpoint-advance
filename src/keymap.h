/**
 * Dewpoint Advance Keyboard Map
 * The MIT License (MIT)
 *
 * Copyright (c) 2026 SUZUKI PLAN.
 */
#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace DewpointKeyMap
{
enum class Button : size_t {
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    L,
    R,
    Start,
    Select,
    RapidA,
    RapidB,
    Count,
};

enum class SpecialKey {
    None,
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Tab,
    Space,
    LeftShift,
    RightShift,
};

struct Binding {
    char character;
    SpecialKey special;
};

constexpr size_t BUTTON_COUNT = static_cast<size_t>(Button::Count);

struct Config {
    std::array<Binding, BUTTON_COUNT> bindings;
};

enum class LoadResult {
    Loaded,
    Missing,
    Unreadable,
};

Config defaultConfig();
const char* buttonName(Button button);
std::string bindingName(const Binding& binding);
bool isAssigned(const Binding& binding);
char buttonCharacter(const Binding& binding);
char buttonCharacter(const Config& config, Button button);

struct RapidFireState {
    unsigned phase = 0;
};

bool advanceRapidFire(RapidFireState* state, bool held);
LoadResult load(
    const std::string& path,
    Config* config,
    std::vector<std::string>* diagnostics,
    std::string* errorMessage);
bool writeDefault(const std::string& path, std::string* errorMessage);
} // namespace DewpointKeyMap
