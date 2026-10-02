// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "Common/CommonTypes.h"
#include "Core/HW/EXI/EXI.h"
#include "Core/HW/GCMemcard/GCMemcard.h"
#include "DiscIO/Enums.h"

namespace MemoryCards
{
using ExpansionInterface::Slot;

enum class ImportResult
{
  Success,
  AlreadyPresent,
  OutOfBlocks,
  OutOfEntries,
  Failed,
};

struct Save
{
  Memcard::Savefile file;
  std::string title;
  std::string comment;

  u8 card_index = 0;
  std::string path;
};

class Card
{
public:
  Card(Slot slot, DiscIO::Region region);

  Card(const Card&) = delete;
  Card& operator=(const Card&) = delete;

  bool IsInserted() const { return m_inserted; }
  bool IsFolder() const { return m_folder; }
  bool IsReadable() const { return m_readable; }
  const std::string& GetPath() const { return m_path; }
  const std::vector<Save>& GetSaves() const { return m_saves; }

  u32 GetFreeBlocks() const;
  u32 GetTotalBlocks() const;

  std::optional<size_t> Find(const Memcard::DEntry& entry) const;

  ImportResult Import(const Memcard::Savefile& savefile, bool replace);
  bool Remove(size_t index);
  bool Erase();

private:
  void Load();
  void LoadFolder();
  void LoadRaw();
  bool CreateRaw();

  bool RemoveFromFolder(size_t index);
  ImportResult ImportToFolder(const Memcard::Savefile& savefile);
  ImportResult ImportToRaw(const Memcard::Savefile& savefile, std::optional<size_t> replaced);

  Slot m_slot;
  DiscIO::Region m_region;
  u16 m_size_mbits;
  bool m_shift_jis;

  bool m_inserted = false;
  bool m_folder = false;
  bool m_readable = false;
  std::string m_path;

  std::optional<Memcard::GCMemcard> m_raw;
  std::vector<Save> m_saves;
};

constexpr std::array<DiscIO::Region, 3> REGIONS = {DiscIO::Region::NTSC_U, DiscIO::Region::PAL,
                                                   DiscIO::Region::NTSC_J};

std::string GetRegionLabel(DiscIO::Region region);
DiscIO::Region GetDefaultRegion();

bool IsInserted(Slot slot);
const char* GetSlotLabel(Slot slot);
Slot GetOtherSlot(Slot slot);

std::string GetGameCode(const Memcard::DEntry& entry);

std::string GetExportDirectory();
std::optional<std::string> Export(const Save& save);

std::optional<std::vector<u32>> DecodeIcon(const Memcard::Savefile& savefile);
}  // namespace MemoryCards
