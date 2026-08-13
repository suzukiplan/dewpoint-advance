#pragma once

namespace DewpointSteamUtils
{
namespace Detail
{
// Steamworks SDK v1.65 replaced IsSteamRunningOnSteamDeck() with
// IsRunningOnSteamHardware(). Steam Deck is value 1 in ESteamHardwareType.
template <typename SteamUtilsType>
auto isRunningOnSteamDeck(SteamUtilsType* utils, int)
    -> decltype(utils->IsRunningOnSteamHardware(), bool())
{
    using HardwareType = decltype(utils->IsRunningOnSteamHardware());
    return utils->IsRunningOnSteamHardware() == static_cast<HardwareType>(1);
}

template <typename SteamUtilsType>
bool isRunningOnSteamDeck(SteamUtilsType* utils, long)
{
    return utils->IsSteamRunningOnSteamDeck();
}
} // namespace Detail

template <typename SteamUtilsType>
bool isRunningOnSteamDeck(SteamUtilsType* utils)
{
    return utils ? Detail::isRunningOnSteamDeck(utils, 0) : false;
}
} // namespace DewpointSteamUtils
