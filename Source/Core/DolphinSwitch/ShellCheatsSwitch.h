// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

namespace brls
{
class View;
}

namespace UICommon
{
class GameFile;
}

namespace Shell
{
brls::View* CreateGameCheatsView(std::shared_ptr<const UICommon::GameFile> game);

void StopCheatDownloads();
}  // namespace Shell
