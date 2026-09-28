// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "UICommon/GameFileCache.h"

namespace Shell
{
using GamePtr = std::shared_ptr<const UICommon::GameFile>;
using Games = std::vector<GamePtr>;

class Library
{
public:
  using ListChangedFn = std::function<void(Games)>;
  using GameUpdatedFn = std::function<void(GamePtr)>;

  Library();
  ~Library();

  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  Games GetGames() const;

  void Refresh(ListChangedFn list_changed, GameUpdatedFn game_updated);
  void Stop();

  void ClearCache();

private:
  Games Snapshot() const;

  UICommon::GameFileCache m_cache;
  std::thread m_thread;
  std::atomic_bool m_halt = false;
  std::atomic_bool m_changed = false;
};
}  // namespace Shell
