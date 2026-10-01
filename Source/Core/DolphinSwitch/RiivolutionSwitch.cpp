// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/RiivolutionSwitch.h"

#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Core/Boot/Boot.h"
#include "DiscIO/Volume.h"
#include "UICommon/GameFile.h"

namespace RiivolutionSwitch
{
namespace
{
bool HasStandardGameID(std::string_view game_id)
{
  return game_id.size() == 4 || game_id.size() == 6;
}

std::string GetConfigPath(std::string_view game_id)
{
  return fmt::format("{}riivolution/config/{}.xml", File::GetUserPath(D_RIIVOLUTION_IDX),
                     game_id.substr(0, 4));
}
}  // namespace

std::string GetPatchDirectory()
{
  return File::GetUserPath(D_RIIVOLUTION_IDX) + "riivolution" DIR_SEP;
}

std::vector<DiscIO::Riivolution::Disc> LoadDiscs(const UICommon::GameFile& game)
{
  std::vector<DiscIO::Riivolution::Disc> discs;
  const std::string& game_id = game.GetGameID();
  if (!HasStandardGameID(game_id))
    return discs;

  const std::optional<DiscIO::Riivolution::Config> config =
      DiscIO::Riivolution::ParseConfigFile(GetConfigPath(game_id));

  for (const std::string& path : Common::DoFileSearch(GetPatchDirectory(), ".xml"))
  {
    std::optional<DiscIO::Riivolution::Disc> parsed = DiscIO::Riivolution::ParseFile(path);
    if (!parsed || !parsed->IsValidForGame(game_id, game.GetRevision(), game.GetDiscNumber()))
      continue;
    if (config)
      DiscIO::Riivolution::ApplyConfigDefaults(&*parsed, *config);
    discs.emplace_back(std::move(*parsed));
  }

  return discs;
}

void SaveChoices(const std::string& game_id, const std::vector<DiscIO::Riivolution::Disc>& discs)
{
  if (!HasStandardGameID(game_id))
    return;

  DiscIO::Riivolution::Config config;
  for (const DiscIO::Riivolution::Disc& disc : discs)
  {
    for (const DiscIO::Riivolution::Section& section : disc.m_sections)
    {
      for (const DiscIO::Riivolution::Option& option : section.m_options)
      {
        std::string id = option.m_id.empty() ? section.m_name + option.m_name : option.m_id;
        config.m_options.push_back({std::move(id), option.m_selected_choice});
      }
    }
  }

  const std::string path = GetConfigPath(game_id);
  if (!DiscIO::Riivolution::WriteConfigFile(path, config))
    ERROR_LOG_FMT(BOOT, "Could not write Riivolution config {}", path);
}

void AddPatches(BootParameters& boot)
{
  if (!std::holds_alternative<BootParameters::Disc>(boot.parameters))
    return;

  const DiscIO::Volume& volume = *std::get<BootParameters::Disc>(boot.parameters).volume;
  auto patches = DiscIO::Riivolution::GenerateRiivolutionPatchesFromConfig(
      File::GetUserPath(D_RIIVOLUTION_IDX), volume.GetGameID(), volume.GetRevision(),
      volume.GetDiscNumber());

  NOTICE_LOG_FMT(BOOT, "Applying {} Riivolution patches", patches.size());
  AddRiivolutionPatches(&boot, std::move(patches));
}
}  // namespace RiivolutionSwitch
