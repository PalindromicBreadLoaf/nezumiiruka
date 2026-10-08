// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/FiguresSwitch.h"

#include <algorithm>
#include <span>
#include <string_view>
#include <utility>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/IOFile.h"
#include "Common/StringUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/IOS/USB/Emulated/Infinity.h"
#include "Core/IOS/USB/Emulated/Skylanders/Skylander.h"
#include "Core/IOS/USB/Emulated/Skylanders/SkylanderFigure.h"
#include "Core/System.h"

namespace FiguresSwitch
{
namespace
{
using IOS::HLE::USB::FigureUIPosition;

constexpr u8 INVALID_PORTAL_SLOT = 0xFF;

std::string WithSeparator(std::string path)
{
  if (!path.empty() && path.back() != DIR_SEP_CHR)
    path += DIR_SEP_CHR;
  return path;
}

std::vector<FigureFile> ListFiles(const std::string& directory,
                                  std::span<const std::string_view> extensions)
{
  File::CreateFullPath(directory);

  std::vector<FigureFile> files;
  for (const std::string& path : Common::DoFileSearch(directory, extensions, true))
  {
    std::string name;
    SplitPath(path, nullptr, &name, nullptr);
    files.push_back({path, std::move(name)});
  }

  std::ranges::sort(files, {}, &FigureFile::name);
  return files;
}

std::string UniquePath(const std::string& directory, const std::string& name,
                       const std::string& extension)
{
  File::CreateFullPath(directory);

  std::string base = name;
  std::ranges::replace_if(
      base, [](char c) { return c == '/' || c == '\\' || c == ':' || c == '"' || c == '?'; }, '_');

  std::string path = directory + base + extension;
  for (int copy = 2; File::Exists(path); ++copy)
    path = fmt::format("{}{} ({}){}", directory, base, copy, extension);
  return path;
}

std::string SkylanderName(const std::pair<u16, u16>& ids)
{
  const auto found = IOS::HLE::USB::list_skylanders.find(ids);
  if (found != IOS::HLE::USB::list_skylanders.end())
    return found->second.name;
  return fmt::format("Unknown (ID {}, variant {})", ids.first, ids.second);
}

bool IsInfinityPieceForSlot(size_t slot, u32 figure)
{
  // The same ranges Dolphin's Qt figure creator filters each slot by.
  const bool character = figure < 0x1E847F;
  const bool play_set = figure > 0x1E8480 && figure < 0x2DC6BF;
  const bool ability = figure > 0x2DC6C0 && figure < 0x3D08FF;
  const bool hexagon = figure > 0x3D0900 && figure < 0x4C4B3F;

  switch (static_cast<FigureUIPosition>(slot))
  {
  case FigureUIPosition::HexagonDiscOne:
    return play_set || hexagon;
  case FigureUIPosition::HexagonDiscTwo:
  case FigureUIPosition::HexagonDiscThree:
    return hexagon;
  case FigureUIPosition::PlayerOne:
  case FigureUIPosition::PlayerTwo:
    return character;
  default:
    return ability;
  }
}
}  // namespace

std::string GetSkylanderDirectory()
{
  const std::string configured = Config::Get(Config::MAIN_SKYLANDERS_PATH);
  if (!configured.empty())
    return WithSeparator(configured);
  return File::GetUserPath(D_USER_IDX) + "Skylanders" DIR_SEP;
}

std::string GetInfinityDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "Infinity" DIR_SEP;
}

std::vector<FigureFile> ListSkylanderFiles()
{
  static constexpr std::array<std::string_view, 4> extensions = {".sky", ".bin", ".dmp", ".dump"};
  return ListFiles(GetSkylanderDirectory(), extensions);
}

std::vector<FigureFile> ListInfinityFiles()
{
  static constexpr std::array<std::string_view, 1> extensions = {".bin"};
  return ListFiles(GetInfinityDirectory(), extensions);
}

std::vector<std::string> GetSkylanderGames()
{
  return {"Spyro's Adventure", "Giants", "Swap Force", "Trap Team", "SuperChargers"};
}

std::vector<Character> GetSkylanderCharacters(size_t game)
{
  std::vector<Character> characters;
  for (const auto& [ids, data] : IOS::HLE::USB::list_skylanders)
  {
    if (static_cast<size_t>(data.game) == game)
      characters.push_back({(u32{ids.first} << 16) | ids.second, data.name});
  }

  std::ranges::sort(characters, {}, &Character::name);
  return characters;
}

std::string GetInfinitySlotName(size_t slot)
{
  switch (static_cast<FigureUIPosition>(slot))
  {
  case FigureUIPosition::HexagonDiscOne:
    return "Play set or power disc";
  case FigureUIPosition::HexagonDiscTwo:
    return "Power disc 2";
  case FigureUIPosition::HexagonDiscThree:
    return "Power disc 3";
  case FigureUIPosition::PlayerOne:
    return "Player 1";
  case FigureUIPosition::P1AbilityOne:
    return "Player 1 ability 1";
  case FigureUIPosition::P1AbilityTwo:
    return "Player 1 ability 2";
  case FigureUIPosition::PlayerTwo:
    return "Player 2";
  case FigureUIPosition::P2AbilityOne:
    return "Player 2 ability 1";
  case FigureUIPosition::P2AbilityTwo:
    return "Player 2 ability 2";
  }
  return {};
}

std::vector<Character> GetInfinityCharacters(size_t slot)
{
  std::vector<Character> characters;
  for (const auto& [name, figure] : IOS::HLE::USB::InfinityBase::GetFigureList())
  {
    if (IsInfinityPieceForSlot(slot, figure))
      characters.push_back({figure, name});
  }

  std::ranges::sort(characters, {}, &Character::name);
  return characters;
}

Figures::Figures(Core::System& system) : m_system(system)
{
}

std::string Figures::LoadSkylander(size_t slot, const std::string& path)
{
  File::IOFile file(path, "r+b");
  if (!file)
    return "Could not open the figure. It may already be on the portal.";

  std::array<u8, IOS::HLE::USB::FIGURE_SIZE> data;
  if (!file.ReadBytes(data.data(), data.size()))
    return "The figure file is too small.";

  RemoveSkylander(slot);

  IOS::HLE::USB::SkylanderPortal& portal = m_system.GetSkylanderPortal();
  const std::pair<u16, u16> ids = portal.CalculateIDs(data);
  const u8 portal_slot =
      portal.LoadSkylander(std::make_unique<IOS::HLE::USB::SkylanderFigure>(std::move(file)));
  if (portal_slot == INVALID_PORTAL_SLOT)
    return "The portal could not take the figure.";

  m_skylanders[slot] = {portal_slot, SkylanderName(ids)};
  return {};
}

std::string Figures::CreateSkylander(size_t slot, const Character& character)
{
  const std::string path = UniquePath(GetSkylanderDirectory(), character.name, ".sky");
  {
    IOS::HLE::USB::SkylanderFigure figure(path);
    if (!figure.Create(static_cast<u16>(character.id >> 16), static_cast<u16>(character.id)))
      return "Could not create the figure file.";
    figure.Close();
  }
  return LoadSkylander(slot, path);
}

void Figures::RemoveSkylander(size_t slot)
{
  SkylanderSlot& entry = m_skylanders[slot];
  if (entry.portal_slot)
    m_system.GetSkylanderPortal().RemoveSkylander(*entry.portal_slot);
  entry = {};
}

std::string Figures::LoadInfinity(size_t slot, const std::string& path)
{
  File::IOFile file(path, "r+b");
  if (!file)
    return "Could not open the figure. It may already be on the base.";

  std::array<u8, IOS::HLE::USB::INFINITY_NUM_BLOCKS * IOS::HLE::USB::INFINITY_BLOCK_SIZE> data;
  if (!file.ReadBytes(data.data(), data.size()))
    return "The figure file is too small.";

  const auto position = static_cast<FigureUIPosition>(slot);
  IOS::HLE::USB::InfinityBase& base = m_system.GetInfinityBase();
  base.RemoveFigure(position);
  m_infinity[slot] = base.LoadFigure(data, std::move(file), position);
  return {};
}

std::string Figures::CreateInfinity(size_t slot, const Character& character)
{
  const std::string path = UniquePath(GetInfinityDirectory(), character.name, ".bin");
  if (!m_system.GetInfinityBase().CreateFigure(path, character.id))
    return "Could not create the figure file.";
  return LoadInfinity(slot, path);
}

void Figures::RemoveInfinity(size_t slot)
{
  m_system.GetInfinityBase().RemoveFigure(static_cast<FigureUIPosition>(slot));
  m_infinity[slot].clear();
}
}  // namespace FiguresSwitch
