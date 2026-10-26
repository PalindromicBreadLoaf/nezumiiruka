// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"

namespace Config
{
enum class PerformanceProfile : int
{
  Stock,
  FasterMemory,
  FasterMemoryAndGpu,
};

enum class GameListSort : int
{
  Title,
  TimePlayed,
  FileName,
};

enum class GameListFilter : int
{
  All,
  GameCube,
  Wii,
};

enum class WiimoteLayout : int
{
  Vertical,
  Sideways,
  Nunchuk,
  Classic,
  SidewaysJoyCon,
  RealSideways,
  RealNunchuk,
};

enum class UpdateChannel : int
{
  Stable,
  Prerelease,
};

enum class UpdateBuild : int
{
  Standard,
  Vulkan,
};

enum class GBAScreens : int
{
  Hidden,
  Small,
  Large,
};

enum class GBAScreenCorner : int
{
  TopLeft,
  TopRight,
  BottomLeft,
  BottomRight,
};

extern const Info<PerformanceProfile> SWITCH_PERFORMANCE_PROFILE;
extern const Info<int> SWITCH_PERFORMANCE_OVERLAY;
extern const Info<std::string> SWITCH_GAME_DIRECTORY;
extern const Info<bool> SWITCH_SEPARATE_GAME_DIRECTORIES;
extern const Info<std::string> SWITCH_GAMECUBE_GAME_DIRECTORY;
extern const Info<std::string> SWITCH_WII_GAME_DIRECTORY;
extern const Info<std::string> SWITCH_USB_GAME_DIRECTORY;
extern const Info<GameListSort> SWITCH_GAME_LIST_SORT;
extern const Info<GameListFilter> SWITCH_GAME_LIST_FILTER;
extern const std::array<Info<WiimoteLayout>, 4> SWITCH_WIIMOTE_LAYOUTS;
extern const std::array<Info<std::string>, 4> SWITCH_PAD_PROFILES;
extern const std::array<Info<std::string>, 4> SWITCH_WIIMOTE_PROFILES;
extern const Info<bool> SWITCH_CHECK_FOR_UPDATES;
extern const Info<UpdateChannel> SWITCH_UPDATE_CHANNEL;
extern const Info<UpdateBuild> SWITCH_UPDATE_BUILD;
extern const Info<std::string> SWITCH_SKIPPED_UPDATE;
extern const Info<std::string> SWITCH_LAST_RUN_VERSION;
extern const Info<GBAScreens> SWITCH_GBA_SCREENS;
extern const Info<GBAScreenCorner> SWITCH_GBA_SCREEN_CORNER;
}  // namespace Config

namespace SwitchSettings
{
constexpr std::array<int, 22> CPU_CLOCK_PERCENTAGES = {50,  55,  60,  65,  70,  75,  80,  85,
                                                       90,  95,  105, 110, 115, 120, 125, 130,
                                                       135, 140, 145, 150, 175, 200};

void ApplyDefaults();

std::string GetDefaultGameDirectory(const Config::Info<std::string>& info);
std::string GetGameDirectory(const Config::Info<std::string>& info);
std::string GetGameDirectory();
std::vector<std::string> GetGameDirectories();

u32 GetPerformanceConfiguration();
}  // namespace SwitchSettings
