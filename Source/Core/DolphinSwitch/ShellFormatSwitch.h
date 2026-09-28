// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <chrono>
#include <string>

namespace UICommon
{
class GameFile;
}

namespace Shell
{
std::string GetTitle(const UICommon::GameFile& game);
std::string GetPlatformName(const UICommon::GameFile& game);
std::string GetShortPlatformName(const UICommon::GameFile& game);
std::string GetRegionName(const UICommon::GameFile& game);

std::string GetGameIdDetail(const UICommon::GameFile& game);

std::string GetTitleIdText(const UICommon::GameFile& game);

std::string GetFormatText(const UICommon::GameFile& game);

std::string FormatTimePlayed(std::chrono::milliseconds time);
}  // namespace Shell
