// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <functional>
#include <memory>

namespace brls
{
class Activity;
}

namespace UICommon
{
class GameFile;
}

namespace Shell
{
brls::Activity* CreateSettingsActivity(std::function<void()> on_closed,
                                       std::function<void()> clear_cache);

brls::Activity* CreateGamePropertiesActivity(std::shared_ptr<const UICommon::GameFile> game,
                                             std::chrono::milliseconds time_played,
                                             std::function<void()> launch);
}  // namespace Shell
