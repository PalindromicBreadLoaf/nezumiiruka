// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellFormatSwitch.h"

#include <mutex>

#include <fmt/format.h>

#include "Core/TitleDatabase.h"
#include "DiscIO/Enums.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"

namespace Shell
{
std::string GetTitle(const UICommon::GameFile& game)
{
  static std::mutex s_mutex;
  static const Core::TitleDatabase s_title_database;

  std::lock_guard lock(s_mutex);
  const std::string& name = game.GetName(s_title_database);
  return name.empty() ? game.GetFileName() : name;
}

std::string GetPlatformName(const UICommon::GameFile& game)
{
  switch (game.GetPlatform())
  {
  case DiscIO::Platform::GameCubeDisc:
    return "GameCube";
  case DiscIO::Platform::Triforce:
    return "Triforce";
  case DiscIO::Platform::WiiDisc:
    return "Wii";
  case DiscIO::Platform::WiiWAD:
    return "WiiWare";
  case DiscIO::Platform::ELFOrDOL:
    return "Homebrew";
  default:
    return "Unknown";
  }
}

std::string GetShortPlatformName(const UICommon::GameFile& game)
{
  switch (game.GetPlatform())
  {
  case DiscIO::Platform::GameCubeDisc:
    return "GC";
  case DiscIO::Platform::Triforce:
    return "Triforce";
  case DiscIO::Platform::WiiDisc:
    return "Wii";
  case DiscIO::Platform::WiiWAD:
    return "WAD";
  case DiscIO::Platform::ELFOrDOL:
    return "Homebrew";
  default:
    return "?";
  }
}

std::string GetRegionName(const UICommon::GameFile& game)
{
  return DiscIO::GetName(game.GetRegion(), false);
}

std::string GetGameIdDetail(const UICommon::GameFile& game)
{
  std::string detail;
  if (game.GetRevision() != 0)
    detail = fmt::format("revision {}", game.GetRevision());
  if (game.IsTwoDiscGame() || game.GetDiscNumber() != 0)
  {
    if (!detail.empty())
      detail += ", ";
    detail += fmt::format("disc {}", game.GetDiscNumber() + 1);
  }
  return detail;
}

std::string GetTitleIdText(const UICommon::GameFile& game)
{
  const u64 title_id = game.GetTitleID();
  if (title_id == 0)
    return {};
  return fmt::format("{:08X}-{:08X}", static_cast<u32>(title_id >> 32), static_cast<u32>(title_id));
}

std::string GetFormatText(const UICommon::GameFile& game)
{
  std::string text = game.GetFileFormatName();
  if (!game.ShouldShowFileFormatDetails())
    return text;

  const std::string& compression = game.GetCompressionMethod();
  if (!compression.empty())
    text += fmt::format(", {}", compression);
  if (game.GetBlockSize() != 0)
    text += fmt::format(", {} blocks", UICommon::FormatSize(game.GetBlockSize(), 0));
  return text;
}

std::string FormatTimePlayed(std::chrono::milliseconds time)
{
  const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(time).count();
  if (minutes == 0)
    return time.count() == 0 ? "Never played" : "Under a minute";
  if (minutes < 60)
    return fmt::format("{} min", minutes);
  return fmt::format("{} h {:02} min", minutes / 60, minutes % 60);
}
}  // namespace Shell
