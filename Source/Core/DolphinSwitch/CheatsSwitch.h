// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <expected>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/ActionReplay.h"
#include "Core/GeckoCode.h"
#include "Core/PatchEngine.h"

namespace CheatsSwitch
{
struct GameCheats
{
  std::vector<Gecko::GeckoCode> gecko;
  std::vector<ActionReplay::ARCode> action_replay;
  std::vector<PatchEngine::Patch> patches;

  bool IsEmpty() const;
  size_t CountEnabled() const;
};

GameCheats Load(const std::string& game_id, u16 revision);
void Save(const std::string& game_id, const GameCheats& cheats);

struct DownloadResult
{
  size_t downloaded = 0;
  size_t added = 0;
};

std::expected<DownloadResult, std::string> DownloadGeckoCodes(const std::string& gametdb_id,
                                                              GameCheats& cheats);

std::vector<std::string> DescribeLines(const Gecko::GeckoCode& code);
std::vector<std::string> DescribeLines(const ActionReplay::ARCode& code);
std::vector<std::string> DescribeLines(const PatchEngine::Patch& patch);
}  // namespace CheatsSwitch
