// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

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

extern const Info<PerformanceProfile> SWITCH_PERFORMANCE_PROFILE;
extern const Info<int> SWITCH_PERFORMANCE_OVERLAY;
extern const Info<std::string> SWITCH_GAME_DIRECTORY;
extern const Info<GameListSort> SWITCH_GAME_LIST_SORT;
extern const Info<GameListFilter> SWITCH_GAME_LIST_FILTER;
}  // namespace Config

namespace SwitchSettings
{
void ApplyDefaults();

std::string GetGameDirectory();

u32 GetPerformanceConfiguration();
}  // namespace SwitchSettings
