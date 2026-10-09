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

// SDK key codes are independent of SDL and Windows virtual key codes.
bool fromKeyCode(int code, Binding* binding);
int keyCode(const Binding& binding);
int get(const Config& config, int buttonId);
bool set(const std::string& path, Config* config, int buttonId, int code,
         std::string* errorMessage);
Config defaultConfig();
const char* buttonName(Button button);
std::string bindingName(const Binding& binding);
bool isAssigned(const Binding& binding);
char buttonCharacter(const Binding& binding);
char buttonCharacter(const Config& config, Button button);

// A layout fallback is transient: only Config supplied by the caller is saved.
template <typename Key>
struct ResolvedMap {
    Config effectiveConfig;
    std::array<Key, BUTTON_COUNT> keys{};
    std::array<bool, BUTTON_COUNT> usedFallback{};
};

template <typename Key, typename Resolver>
ResolvedMap<Key> resolve(const Config& configured, Resolver resolver)
{
    ResolvedMap<Key> result{};
    result.effectiveConfig = configured;
    const Config defaults = defaultConfig();
    for (size_t index = 0; index < BUTTON_COUNT; ++index) {
        const Binding& binding = configured.bindings[index];
        if (!isAssigned(binding) || resolver(binding, &result.keys[index])) {
            continue;
        }
        result.usedFallback[index] = true;
        result.effectiveConfig.bindings[index] = defaults.bindings[index];
        const Binding& fallback = defaults.bindings[index];
        result.keys[index] = Key{};
        if (isAssigned(fallback) && !resolver(fallback, &result.keys[index])) {
            result.keys[index] = static_cast<unsigned char>(fallback.character);
        }
    }
    return result;
}

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
