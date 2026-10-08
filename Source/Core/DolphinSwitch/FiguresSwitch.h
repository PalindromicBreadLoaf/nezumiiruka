// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"

namespace Core
{
class System;
}

namespace FiguresSwitch
{
constexpr size_t SKYLANDER_SLOTS = 16;
constexpr size_t INFINITY_SLOTS = 9;

struct FigureFile
{
  std::string path;
  std::string name;
};

struct Character
{
  u32 id;
  std::string name;
};

std::string GetSkylanderDirectory();
std::string GetInfinityDirectory();

std::vector<FigureFile> ListSkylanderFiles();
std::vector<FigureFile> ListInfinityFiles();

std::vector<std::string> GetSkylanderGames();
std::vector<Character> GetSkylanderCharacters(size_t game);

std::string GetInfinitySlotName(size_t slot);
std::vector<Character> GetInfinityCharacters(size_t slot);

class Figures
{
public:
  explicit Figures(Core::System& system);

  const std::string& GetSkylanderName(size_t slot) const { return m_skylanders[slot].name; }
  const std::string& GetInfinityName(size_t slot) const { return m_infinity[slot]; }

  std::string LoadSkylander(size_t slot, const std::string& path);
  std::string CreateSkylander(size_t slot, const Character& character);
  void RemoveSkylander(size_t slot);

  std::string LoadInfinity(size_t slot, const std::string& path);
  std::string CreateInfinity(size_t slot, const Character& character);
  void RemoveInfinity(size_t slot);

private:
  struct SkylanderSlot
  {
    std::optional<u8> portal_slot;
    std::string name;
  };

  Core::System& m_system;
  std::array<SkylanderSlot, SKYLANDER_SLOTS> m_skylanders;
  std::array<std::string, INFINITY_SLOTS> m_infinity;
};
}  // namespace FiguresSwitch
