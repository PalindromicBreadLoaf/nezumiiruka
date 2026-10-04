// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/CheatsSwitch.h"

#include <algorithm>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/ConfigManager.h"
#include "Core/GeckoCodeConfig.h"

namespace CheatsSwitch
{
namespace
{
std::string GetLocalIniPath(const std::string& game_id)
{
  return File::GetUserPath(D_GAMESETTINGS_IDX) + game_id + ".ini";
}
}  // namespace

bool GameCheats::IsEmpty() const
{
  return gecko.empty() && action_replay.empty() && patches.empty();
}

size_t GameCheats::CountEnabled() const
{
  const auto enabled = [](const auto& code) { return code.enabled; };
  return std::ranges::count_if(gecko, enabled) + std::ranges::count_if(action_replay, enabled) +
         std::ranges::count_if(patches, enabled);
}

GameCheats Load(const std::string& game_id, u16 revision)
{
  GameCheats cheats;
  if (game_id.empty())
    return cheats;

  Common::IniFile local_ini;
  local_ini.Load(GetLocalIniPath(game_id));
  const Common::IniFile global_ini = SConfig::LoadDefaultGameIni(game_id, revision);

  cheats.gecko = Gecko::LoadCodes(global_ini, local_ini);
  cheats.action_replay = ActionReplay::LoadCodes(global_ini, local_ini);
  PatchEngine::LoadPatchSection("OnFrame", &cheats.patches, global_ini, local_ini);
  return cheats;
}

void Save(const std::string& game_id, const GameCheats& cheats)
{
  if (game_id.empty())
    return;

  const std::string path = GetLocalIniPath(game_id);
  Common::IniFile local_ini;
  local_ini.Load(path);
  Gecko::SaveCodes(local_ini, cheats.gecko);
  ActionReplay::SaveCodes(&local_ini, cheats.action_replay);
  PatchEngine::SavePatchSection(&local_ini, cheats.patches);
  if (!local_ini.Save(path))
    ERROR_LOG_FMT(COMMON, "Could not save cheats to {}", path);
}

std::expected<DownloadResult, std::string> DownloadGeckoCodes(const std::string& gametdb_id,
                                                              GameCheats& cheats)
{
  if (gametdb_id.empty())
    return std::unexpected("This game has no ID to look its codes up by.");

  const auto downloaded = Gecko::DownloadCodes(gametdb_id);
  if (!downloaded)
  {
    std::string message = "Could not download Gecko codes. Check your internet connection or try "
                          "again later.";
    if (downloaded.error() > 0)
      message += fmt::format("\n\nThe server responded with HTTP {}.", downloaded.error());
    return std::unexpected(std::move(message));
  }

  if (downloaded->empty())
    return std::unexpected("The code server has no Gecko codes for this game.");

  DownloadResult result{.downloaded = downloaded->size()};
  for (const Gecko::GeckoCode& code : *downloaded)
  {
    if (std::ranges::find(cheats.gecko, code) != cheats.gecko.end())
      continue;
    cheats.gecko.push_back(code);
    ++result.added;
  }
  return result;
}

std::vector<std::string> DescribeLines(const Gecko::GeckoCode& code)
{
  std::vector<std::string> lines;
  for (const Gecko::GeckoCode::Code& line : code.codes)
  {
    lines.push_back(line.original_line.empty() ?
                        fmt::format("{:08X} {:08X}", line.address, line.data) :
                        line.original_line);
  }
  return lines;
}

std::vector<std::string> DescribeLines(const ActionReplay::ARCode& code)
{
  std::vector<std::string> lines;
  for (const ActionReplay::AREntry& entry : code.ops)
    lines.push_back(ActionReplay::SerializeLine(entry));
  return lines;
}

std::vector<std::string> DescribeLines(const PatchEngine::Patch& patch)
{
  std::vector<std::string> lines;
  for (const PatchEngine::PatchEntry& entry : patch.entries)
    lines.push_back(PatchEngine::SerializeLine(entry));
  return lines;
}
}  // namespace CheatsSwitch
