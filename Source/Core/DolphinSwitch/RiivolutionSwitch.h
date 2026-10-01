// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include "DiscIO/RiivolutionParser.h"

struct BootParameters;

namespace UICommon
{
class GameFile;
}

namespace RiivolutionSwitch
{
std::string GetPatchDirectory();

std::vector<DiscIO::Riivolution::Disc> LoadDiscs(const UICommon::GameFile& game);

void SaveChoices(const std::string& game_id, const std::vector<DiscIO::Riivolution::Disc>& discs);

void AddPatches(BootParameters& boot);
}  // namespace RiivolutionSwitch
