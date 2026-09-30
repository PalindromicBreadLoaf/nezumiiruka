// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

namespace brls
{
class View;
}

namespace Shell
{
brls::View* CreateAchievementAccountView();

brls::View* CreateGameAchievementsView(const std::string& path);

void StopAchievementFetches();
}  // namespace Shell
