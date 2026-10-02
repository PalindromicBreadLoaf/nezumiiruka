// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

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
brls::Activity* CreateMemoryCardsActivity();
brls::Activity* CreateMemoryCardsActivity(const UICommon::GameFile& game);
}  // namespace Shell
