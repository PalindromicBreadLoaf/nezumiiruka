// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/PerformanceOverlaySwitch.h"

#include <algorithm>
#include <array>

#include "Common/Config/Config.h"
#include "Core/Config/GraphicsSettings.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "VideoCommon/OnScreenDisplay.h"

namespace PerfOverlay
{
namespace
{
enum Level : int
{
  Off = 0,
  Stats = 1,
  StatsAndGraphs = 2,
  Count = 3,
};

constexpr std::array<const char*, Level::Count> LEVEL_NAMES = {
    "Performance overlay: off",
    "Performance overlay: stats",
    "Performance overlay: stats + graphs",
};

void Apply(int level)
{
  const bool stats = level >= Level::Stats;
  const bool graphs = level >= Level::StatsAndGraphs;

  Config::SetCurrent(Config::GFX_SHOW_FPS, stats);
  Config::SetCurrent(Config::GFX_SHOW_FTIMES, stats);
  Config::SetCurrent(Config::GFX_SHOW_VPS, stats);
  Config::SetCurrent(Config::GFX_SHOW_VTIMES, stats);
  Config::SetCurrent(Config::GFX_SHOW_SPEED, stats);
  Config::SetCurrent(Config::GFX_SHOW_SPEED_COLORS, stats);
  Config::SetCurrent(Config::GFX_SHOW_INTERNAL_RESOLUTION, stats);
  Config::SetCurrent(Config::GFX_SHOW_GRAPHS, graphs);
}
}  // namespace

void ApplyCurrentLevel()
{
  Apply(Config::Get(Config::SWITCH_PERFORMANCE_OVERLAY));
}

void CycleLevel()
{
  const int current =
      std::clamp(Config::Get(Config::SWITCH_PERFORMANCE_OVERLAY), 0, Level::Count - 1);
  const int level = (current + 1) % Level::Count;
  Config::SetBase(Config::SWITCH_PERFORMANCE_OVERLAY, level);
  Apply(level);
  OSD::AddMessage(LEVEL_NAMES[level]);
}
}  // namespace PerfOverlay
