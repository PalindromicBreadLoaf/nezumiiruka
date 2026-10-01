// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>
#include <vector>

#include <borealis.hpp>

#include "Core/TimePlayed.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "DolphinSwitch/ShellLibrarySwitch.h"
#include "DolphinSwitch/ShellSwitch.h"

namespace Shell
{
class GameCell;

class GameListView : public brls::Box
{
public:
  GameListView(Library& library, BootRequest& chosen, const std::string& focus_path);
  ~GameListView() override;

  GameListView(const GameListView&) = delete;
  GameListView& operator=(const GameListView&) = delete;

private:
  struct Section
  {
    std::string label;
    size_t first_row;
  };

  void StartRefresh();
  void SetGames(Games games);
  void UpdateGame(const GamePtr& game);

  void Rebuild();
  void RebuildFilterBar();
  std::string GetSectionLabel(const UICommon::GameFile& game) const;
  void FocusRow(size_t row);

  void ShowDetails(const GamePtr& game);
  void ShowCover(const UICommon::GameFile& game);
  std::chrono::milliseconds GetTimePlayed(const UICommon::GameFile& game) const;

  void Launch(const GamePtr& game, bool riivolution = false);
  void LaunchSystemMenu();
  void OpenProperties();
  void OpenSettings();
  void OnSettingsClosed();
  void ClearCache();

  void JumpSection(int direction);
  void OpenJumpMenu();
  void CycleFilter(int direction);

  brls::Box* AddDetailRow(brls::Box* container, const std::string& key, brls::Label** value);

  Library& m_library;
  BootRequest& m_chosen;
  std::string m_focused_path;
  std::string m_game_directory;

  TimePlayed m_time_played;
  Config::GameListFilter m_filter;
  Config::GameListSort m_sort;
  bool m_use_covers;
  bool m_rebuild_pending = false;
  brls::VoidEvent::Subscription m_run_loop_subscription;

  Games m_games;
  Games m_visible;
  std::vector<GameCell*> m_cells;
  std::vector<Section> m_sections;

  brls::Box* m_filter_bar = nullptr;
  brls::ScrollingFrame* m_scroll = nullptr;
  brls::Box* m_list = nullptr;

  brls::Box* m_details = nullptr;
  brls::Image* m_cover = nullptr;
  brls::Label* m_cover_placeholder = nullptr;
  brls::Label* m_title = nullptr;
  brls::Label* m_maker = nullptr;
  brls::Label* m_platform = nullptr;
  brls::Label* m_region = nullptr;
  brls::Label* m_game_id = nullptr;
  brls::Label* m_title_id = nullptr;
  brls::Label* m_time = nullptr;
  brls::Label* m_file = nullptr;
  brls::Label* m_size = nullptr;
  brls::Label* m_format = nullptr;
  brls::Box* m_title_id_row = nullptr;
  std::string m_cover_path;
};
}  // namespace Shell
