// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellLibrarySwitch.h"

#include <string_view>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/Thread.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "UICommon/GameFile.h"

namespace Shell
{
Library::Library()
{
  m_cache.Load();
}

Library::~Library()
{
  Stop();
}

Games Library::GetGames() const
{
  return Snapshot();
}

Games Library::Snapshot() const
{
  Games games;
  games.reserve(m_cache.GetSize());
  m_cache.ForEach([&games](const GamePtr& game) { games.push_back(game); });
  return games;
}

void Library::Refresh(ListChangedFn list_changed, GameUpdatedFn game_updated)
{
  Stop();

  m_thread = std::thread([this, directory = SwitchSettings::GetGameDirectory(),
                          list_changed = std::move(list_changed),
                          game_updated = std::move(game_updated)] {
    Common::SetCurrentThreadName("Game list scan");

    const std::string_view directory_view = directory;
    const std::vector<std::string> paths = UICommon::FindAllGamePaths({&directory_view, 1}, true);

    if (m_cache.Update(paths, {}, {}, m_halt))
    {
      m_changed = true;
      if (!m_halt)
        list_changed(Snapshot());
    }

    if (m_halt)
      return;

    if (m_cache.UpdateAdditionalMetadata(game_updated, m_halt))
      m_changed = true;

    if (m_changed.exchange(false))
      m_cache.Save();
  });
}

void Library::Stop()
{
  if (!m_thread.joinable())
    return;

  m_halt = true;
  m_thread.join();
  m_halt = false;

  if (m_changed.exchange(false))
    m_cache.Save();
}

void Library::ClearCache()
{
  Stop();

  m_cache.Clear(UICommon::GameFileCache::DeleteOnDisk::Yes);

  const std::string cover_directory = File::GetUserPath(D_COVERCACHE_IDX);
  for (const File::FSTEntry& entry : File::ScanDirectoryTree(cover_directory, false).children)
  {
    if (!entry.isDirectory && entry.virtualName.ends_with(".png"))
      File::Delete(entry.physicalName);
  }

  NOTICE_LOG_FMT(COMMON, "Cleared the game list cache and {}", cover_directory);
}
}  // namespace Shell
