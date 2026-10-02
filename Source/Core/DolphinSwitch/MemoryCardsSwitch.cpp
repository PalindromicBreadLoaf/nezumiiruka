// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/MemoryCardsSwitch.h"

#include <algorithm>
#include <cstring>
#include <tuple>
#include <variant>

#include <fmt/format.h>

#include "Common/ColorUtil.h"
#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "Common/Timer.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/EXI/EXI_DeviceIPL.h"
#include "Core/HW/GCMemcard/GCMemcardUtils.h"
#include "Core/HW/Sram.h"

namespace MemoryCards
{
namespace
{
constexpr u32 NO_OFFSET = 0xFFFFFFFF;

u16 GetConfiguredSize()
{
  const int size_override = Config::Get(Config::MAIN_MEMORY_CARD_SIZE);
  if (size_override >= 0 && size_override <= 4)
    return Memcard::MBIT_SIZE_MEMORY_CARD_59 << size_override;
  return Memcard::MBIT_SIZE_MEMORY_CARD_2043;
}

std::optional<std::vector<u8>> ReadBytes(const Memcard::Savefile& savefile, u32 offset, u32 length)
{
  const size_t size = savefile.blocks.size() * Memcard::BLOCK_SIZE;
  if (offset == NO_OFFSET || offset > size || length > size - offset)
    return std::nullopt;

  std::vector<u8> bytes(length);
  for (u32 i = 0; i < length; ++i)
  {
    const size_t position = offset + i;
    bytes[i] =
        savefile.blocks[position / Memcard::BLOCK_SIZE].m_block[position % Memcard::BLOCK_SIZE];
  }
  return bytes;
}

std::string DecodeString(const u8* data, size_t length, bool shift_jis)
{
  const std::string_view raw(reinterpret_cast<const char*>(data), length);
  std::string text = shift_jis ? SHIFTJISToUTF8(raw) : CP1252ToUTF8(raw);
  text = text.substr(0, text.find('\0'));
  return std::string(StripWhitespace(text));
}

Save MakeSave(Memcard::Savefile savefile, bool shift_jis)
{
  Save save;
  const Memcard::DEntry& entry = savefile.dir_entry;
  if (const auto comments =
          ReadBytes(savefile, entry.m_comments_address, Memcard::DENTRY_STRLEN * 2))
  {
    save.title = DecodeString(comments->data(), Memcard::DENTRY_STRLEN, shift_jis);
    save.comment =
        DecodeString(comments->data() + Memcard::DENTRY_STRLEN, Memcard::DENTRY_STRLEN, shift_jis);
  }
  if (save.title.empty())
  {
    save.title =
        fmt::format("{} {}", GetGameCode(entry),
                    DecodeString(entry.m_filename.data(), entry.m_filename.size(), shift_jis));
  }
  save.file = std::move(savefile);
  return save;
}

u32 CountBlocks(const std::vector<Save>& saves)
{
  u32 blocks = 0;
  for (const Save& save : saves)
    blocks += static_cast<u32>(save.file.blocks.size());
  return blocks;
}
}  // namespace

Card::Card(Slot slot, DiscIO::Region region)
    : m_slot(slot), m_region(Config::ToGameCubeRegion(region)), m_size_mbits(GetConfiguredSize()),
      m_shift_jis(m_region == DiscIO::Region::NTSC_J)
{
  Load();
}

void Card::Load()
{
  m_inserted = MemoryCards::IsInserted(m_slot);
  m_folder = Config::Get(Config::GetInfoForEXIDevice(m_slot)) !=
             ExpansionInterface::EXIDeviceType::MemoryCard;
  m_readable = false;
  m_raw.reset();
  m_saves.clear();
  m_path.clear();

  if (!m_inserted)
    return;

  if (m_folder)
    LoadFolder();
  else
    LoadRaw();

  std::ranges::sort(m_saves, [](const Save& a, const Save& b) {
    return std::tie(a.title, a.comment) < std::tie(b.title, b.comment);
  });
}

void Card::LoadFolder()
{
  m_path = Config::GetGCIFolderPath(m_slot, m_region);
  m_readable = true;

  for (const std::string& path : Common::DoFileSearch(m_path, ".gci"))
  {
    auto result = Memcard::ReadSavefile(path);
    auto* savefile = std::get_if<Memcard::Savefile>(&result);
    if (!savefile)
      continue;

    Save save = MakeSave(std::move(*savefile), m_shift_jis);
    save.path = path;
    m_saves.push_back(std::move(save));
  }
}

void Card::LoadRaw()
{
  m_path = Config::GetMemcardPath(m_slot, m_region, m_size_mbits);

  if (!File::Exists(m_path))
  {
    m_readable = true;
    return;
  }

  auto [error, card] = Memcard::GCMemcard::Open(m_path);
  if (!card)
    return;

  m_raw = std::move(card);
  m_readable = true;

  for (u8 i = 0; i < m_raw->GetNumFiles(); ++i)
  {
    const u8 index = m_raw->GetFileIndex(i);
    auto savefile = m_raw->ExportFile(index);
    if (!savefile)
      continue;

    Save save = MakeSave(std::move(*savefile), m_raw->IsShiftJIS());
    save.card_index = index;
    m_saves.push_back(std::move(save));
  }
}

bool Card::CreateRaw()
{
  File::CreateFullPath(m_path);

  const CardFlashId flash_id{};
  const u64 format_time =
      Common::Timer::GetLocalTimeSinceJan1970() - ExpansionInterface::CEXIIPL::GC_EPOCH;
  m_raw =
      Memcard::GCMemcard::Create(m_path, flash_id, m_size_mbits, m_shift_jis, 0, 0, format_time);
  return m_raw.has_value();
}

u32 Card::GetFreeBlocks() const
{
  if (m_raw)
    return m_raw->GetFreeBlocks();

  const u32 total = Memcard::MbitToFreeBlocks(m_size_mbits);
  return total - std::min(total, CountBlocks(m_saves));
}

u32 Card::GetTotalBlocks() const
{
  if (m_raw)
    return m_raw->GetFreeBlocks() + CountBlocks(m_saves);
  return Memcard::MbitToFreeBlocks(m_size_mbits);
}

std::optional<size_t> Card::Find(const Memcard::DEntry& entry) const
{
  for (size_t i = 0; i < m_saves.size(); ++i)
  {
    if (Memcard::HasSameIdentity(m_saves[i].file.dir_entry, entry))
      return i;
  }
  return std::nullopt;
}

ImportResult Card::Import(const Memcard::Savefile& savefile, bool replace)
{
  if (!m_inserted || !m_readable)
    return ImportResult::Failed;

  const std::optional<size_t> existing = Find(savefile.dir_entry);
  if (existing && !replace)
    return ImportResult::AlreadyPresent;

  const u32 replaced_blocks =
      existing ? static_cast<u32>(m_saves[*existing].file.blocks.size()) : 0;
  const size_t entries = m_saves.size() - (existing ? 1 : 0);
  if (entries >= Memcard::DIRLEN)
    return ImportResult::OutOfEntries;
  if (savefile.blocks.size() > GetFreeBlocks() + replaced_blocks)
    return ImportResult::OutOfBlocks;

  const ImportResult result = m_folder ? ImportToFolder(savefile) : ImportToRaw(savefile, existing);
  if (result == ImportResult::Success && existing && m_folder)
  {
    const std::string target =
        m_path + DIR_SEP + Memcard::GenerateFilename(savefile.dir_entry) + ".gci";
    if (m_saves[*existing].path != target)
      File::Delete(m_saves[*existing].path);
  }

  Load();
  return result;
}

ImportResult Card::ImportToFolder(const Memcard::Savefile& savefile)
{
  File::CreateFullPath(m_path + DIR_SEP);
  const std::string target =
      m_path + DIR_SEP + Memcard::GenerateFilename(savefile.dir_entry) + ".gci";
  return Memcard::WriteSavefile(target, savefile, Memcard::SavefileFormat::GCI) ?
             ImportResult::Success :
             ImportResult::Failed;
}

ImportResult Card::ImportToRaw(const Memcard::Savefile& savefile, std::optional<size_t> replaced)
{
  if (!m_raw && !CreateRaw())
    return ImportResult::Failed;

  if (replaced && m_raw->RemoveFile(m_saves[*replaced].card_index) !=
                      Memcard::GCMemcardRemoveFileRetVal::SUCCESS)
  {
    return ImportResult::Failed;
  }

  switch (m_raw->ImportFile(savefile))
  {
  case Memcard::GCMemcardImportFileRetVal::SUCCESS:
    return m_raw->Save() ? ImportResult::Success : ImportResult::Failed;
  case Memcard::GCMemcardImportFileRetVal::OUTOFDIRENTRIES:
    return ImportResult::OutOfEntries;
  case Memcard::GCMemcardImportFileRetVal::OUTOFBLOCKS:
    return ImportResult::OutOfBlocks;
  case Memcard::GCMemcardImportFileRetVal::TITLEPRESENT:
    return ImportResult::AlreadyPresent;
  default:
    return ImportResult::Failed;
  }
}

bool Card::Remove(size_t index)
{
  if (index >= m_saves.size())
    return false;

  bool removed;
  if (m_folder)
  {
    removed = File::Delete(m_saves[index].path);
  }
  else
  {
    removed = m_raw &&
              m_raw->RemoveFile(m_saves[index].card_index) ==
                  Memcard::GCMemcardRemoveFileRetVal::SUCCESS &&
              m_raw->Save();
  }

  Load();
  return removed;
}

bool Card::Erase()
{
  if (!m_inserted)
    return false;

  bool erased = true;
  if (m_folder)
  {
    for (const Save& save : m_saves)
      erased &= File::Delete(save.path);
  }
  else
  {
    erased = CreateRaw();
  }

  Load();
  return erased;
}

std::string GetRegionLabel(DiscIO::Region region)
{
  switch (region)
  {
  case DiscIO::Region::NTSC_J:
    return "Japan";
  case DiscIO::Region::PAL:
    return "Europe";
  case DiscIO::Region::NTSC_U:
  default:
    return "USA";
  }
}

DiscIO::Region GetDefaultRegion()
{
  return Config::ToGameCubeRegion(Config::Get(Config::MAIN_FALLBACK_REGION));
}

bool IsInserted(Slot slot)
{
  using ExpansionInterface::EXIDeviceType;
  const EXIDeviceType device = Config::Get(Config::GetInfoForEXIDevice(slot));
  return device == EXIDeviceType::MemoryCard || device == EXIDeviceType::MemoryCardFolder;
}

const char* GetSlotLabel(Slot slot)
{
  return slot == Slot::A ? "Slot A" : "Slot B";
}

Slot GetOtherSlot(Slot slot)
{
  return slot == Slot::A ? Slot::B : Slot::A;
}

std::string GetGameCode(const Memcard::DEntry& entry)
{
  std::string code(reinterpret_cast<const char*>(entry.m_gamecode.data()), entry.m_gamecode.size());
  return code.substr(0, code.find('\0'));
}

std::string GetExportDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "GCSaves" DIR_SEP;
}

std::optional<std::string> Export(const Save& save)
{
  const std::string directory = GetExportDirectory();
  File::CreateFullPath(directory);

  const std::string path = directory + Memcard::GenerateFilename(save.file.dir_entry) + ".gci";
  if (!Memcard::WriteSavefile(path, save.file, Memcard::SavefileFormat::GCI))
    return std::nullopt;
  return path;
}

std::optional<std::vector<u32>> DecodeIcon(const Memcard::Savefile& savefile)
{
  using namespace Memcard;
  const DEntry& entry = savefile.dir_entry;
  u32 offset = entry.m_image_offset;
  if (offset == NO_OFFSET)
    return std::nullopt;

  constexpr u32 banner_pixels = MEMORY_CARD_BANNER_WIDTH * MEMORY_CARD_BANNER_HEIGHT;
  constexpr u32 palette_bytes = MEMORY_CARD_CI8_PALETTE_ENTRIES * 2;
  const u8 banner_format = entry.m_banner_and_icon_flags & 0b11;
  if (banner_format == MEMORY_CARD_BANNER_FORMAT_CI8)
    offset += banner_pixels + palette_bytes;
  else if (banner_format == MEMORY_CARD_BANNER_FORMAT_RGB5A3)
    offset += banner_pixels * 2;

  constexpr u32 icon_pixels = MEMORY_CARD_ICON_WIDTH * MEMORY_CARD_ICON_HEIGHT;
  const u16 formats = entry.m_icon_format;
  const u16 delays = entry.m_animation_speed;
  u32 frames_length = 0;
  bool shared_palette = false;
  for (u32 i = 0; i < MEMORY_CARD_ICON_ANIMATION_MAX_FRAMES; ++i)
  {
    if (((delays >> (2 * i)) & 0b11) == 0)
      break;

    switch ((formats >> (2 * i)) & 0b11)
    {
    case MEMORY_CARD_ICON_FORMAT_CI8_SHARED_PALETTE:
      frames_length += icon_pixels;
      shared_palette = true;
      break;
    case MEMORY_CARD_ICON_FORMAT_RGB5A3:
      frames_length += icon_pixels * 2;
      break;
    case MEMORY_CARD_ICON_FORMAT_CI8_UNIQUE_PALETTE:
      frames_length += icon_pixels + palette_bytes;
      break;
    }
  }

  const u8 format = formats & 0b11;
  if (format == 0 || frames_length == 0)
    return std::nullopt;

  const auto data =
      ReadBytes(savefile, offset, frames_length + (shared_palette ? palette_bytes : 0));
  if (!data)
    return std::nullopt;

  std::vector<u32> rgba(icon_pixels);
  std::array<u16, MEMORY_CARD_CI8_PALETTE_ENTRIES> palette;
  if (format == MEMORY_CARD_ICON_FORMAT_RGB5A3)
  {
    std::array<u16, icon_pixels> pixels;
    std::memcpy(pixels.data(), data->data(), icon_pixels * 2);
    Common::Decode5A3Image(rgba.data(), pixels.data(), MEMORY_CARD_ICON_WIDTH,
                           MEMORY_CARD_ICON_HEIGHT);
    return rgba;
  }

  const u32 palette_offset =
      format == MEMORY_CARD_ICON_FORMAT_CI8_SHARED_PALETTE ? frames_length : icon_pixels;
  std::memcpy(palette.data(), data->data() + palette_offset, palette_bytes);
  Common::DecodeCI8Image(rgba.data(), data->data(), palette.data(), MEMORY_CARD_ICON_WIDTH,
                         MEMORY_CARD_ICON_HEIGHT);
  return rgba;
}
}  // namespace MemoryCards
