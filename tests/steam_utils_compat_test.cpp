#include "steam_utils_compat.hpp"

#include <cassert>

namespace
{
struct LegacySteamUtils {
    bool runningOnSteamDeck;

    bool IsSteamRunningOnSteamDeck()
    {
        return runningOnSteamDeck;
    }
};

enum class SteamHardwareType {
    None = 0,
    SteamDeck = 1,
    SteamMachine = 2,
};

struct CurrentSteamUtils {
    SteamHardwareType hardwareType;

    SteamHardwareType IsRunningOnSteamHardware()
    {
        return hardwareType;
    }
};
} // namespace

int main()
{
    LegacySteamUtils legacyDeck{true};
    LegacySteamUtils legacyDesktop{false};
    assert(DewpointSteamUtils::isRunningOnSteamDeck(&legacyDeck));
    assert(!DewpointSteamUtils::isRunningOnSteamDeck(&legacyDesktop));
    assert(!DewpointSteamUtils::isRunningOnSteamDeck(static_cast<LegacySteamUtils*>(nullptr)));

    CurrentSteamUtils currentDeck{SteamHardwareType::SteamDeck};
    CurrentSteamUtils currentDesktop{SteamHardwareType::None};
    CurrentSteamUtils currentMachine{SteamHardwareType::SteamMachine};
    assert(DewpointSteamUtils::isRunningOnSteamDeck(&currentDeck));
    assert(!DewpointSteamUtils::isRunningOnSteamDeck(&currentDesktop));
    assert(!DewpointSteamUtils::isRunningOnSteamDeck(&currentMachine));
    assert(!DewpointSteamUtils::isRunningOnSteamDeck(static_cast<CurrentSteamUtils*>(nullptr)));
    return 0;
}
