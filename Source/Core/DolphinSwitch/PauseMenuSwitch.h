// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "DolphinSwitch/CheatsSwitch.h"
#include "DolphinSwitch/ControllerProfilesSwitch.h"
#include "DolphinSwitch/FiguresSwitch.h"

namespace Core
{
class System;
}

struct ImFont;
struct ImGuiContext;

class PauseMenu
{
public:
  PauseMenu(Core::System& system, std::string disc_path, std::function<void()> request_quit);
  ~PauseMenu();

  PauseMenu(const PauseMenu&) = delete;
  PauseMenu& operator=(const PauseMenu&) = delete;

  void Update(u64 buttons);

  bool IsOpen() const { return m_state != State::Closed; }

  bool IsBlockingInput() const { return m_blocking_input.load(std::memory_order_relaxed); }

private:
  enum class State
  {
    Closed,
    Open,
    Closing,
  };

  enum class PageId
  {
    Main,
    SaveState,
    LoadState,
    Settings,
    Controls,
    Cheats,
    Discs,
    Figures,
    FigureFiles,
    FigureGames,
    FigureCharacters,
    Confirm,
  };

  enum class RowKind
  {
    Action,
    Submenu,
    Choice,
    Header,
  };

  struct Row
  {
    RowKind kind = RowKind::Action;
    std::string label;
    std::string value;
    bool enabled = true;
    std::function<void()> activate;
    std::function<void(int)> change;
  };

  struct Page
  {
    PageId id;
    int selected = 0;
    int first_visible = 0;
  };

  struct Confirmation
  {
    std::string question;
    std::string action;
    std::function<void()> on_confirm;
  };

  struct Disc
  {
    int number;
    std::string path;
    std::string file_name;
  };

  struct FigureTarget
  {
    bool infinity = false;
    size_t slot = 0;
    size_t game = 0;
  };

  struct ViewRow
  {
    RowKind kind;
    std::string label;
    std::string value;
    bool enabled;
  };

  struct View
  {
    bool visible = false;
    std::string title;
    std::string page_title;
    std::string message;
    std::string status;
    std::vector<ViewRow> rows;
    int selected = 0;
    int first_visible = 0;
  };

  void TryOpen();
  void Close(std::function<void()> after_resume = {});
  void FinishClose();

  void HandleInput(u64 buttons, u64 pressed);
  bool TakeRepeat(size_t direction, bool held, bool pressed);
  void MoveSelection(int delta);
  void JumpSelection(int delta);
  void ChangeSelection(int delta);
  void Push(PageId id);
  void Back();
  void Confirm(std::string question, std::string action, std::function<void()> on_confirm);
  void Defer(std::string status, std::function<void()> action);

  Page& CurrentPage() { return m_pages.back(); }
  void Rebuild();
  void ClampSelection();
  std::string GetPageTitle() const;

  std::vector<Row> BuildMain();
  std::vector<Row> BuildSlots(bool save);
  std::vector<Row> BuildSettings();
  std::vector<Row> BuildControls();
  std::vector<Row> BuildCheats();
  std::vector<Row> BuildDiscs();
  std::vector<Row> BuildFigures();
  std::vector<Row> BuildFigureFiles();
  std::vector<Row> BuildFigureGames();
  std::vector<Row> BuildFigureCharacters();
  std::vector<Row> BuildConfirm();

  template <typename T>
  using Options = std::vector<std::pair<std::string, T>>;
  template <typename T>
  static void AddChoice(std::vector<Row>& rows, std::string label, Options<T> options,
                        const T& current, std::function<void(const T&)> write);

  Row ProfileRow(ControllerProfiles::Kind kind, int slot, std::string label);
  void ApplyProfile(ControllerProfiles::Kind kind, int slot, const std::string& name);
  void FindDiscs();
  bool HasFigureDevices() const;
  std::string DescribeFigureTarget() const;
  void FinishPlacingFigure(const std::string& error, const std::string& success);
  void ApplyCheats();

  template <typename T>
  void WriteForGame(const Config::Info<T>& info, const T& value);
  template <typename T>
  void WriteGlobal(const Config::Info<T>& info, const T& value);

  void RunStateJob(const std::function<void()>& job);

  void Publish();
  void Redraw();

  void Draw();
  ImFont* GetFont();

  Core::System& m_system;
  std::string m_disc_path;
  std::function<void()> m_request_quit;

  State m_state = State::Closed;
  std::atomic<bool> m_blocking_input{false};
  std::atomic<bool> m_state_job_pending{false};

  u64 m_previous_buttons = 0;
  std::optional<std::chrono::steady_clock::time_point> m_minus_pressed_at;
  std::array<std::chrono::steady_clock::time_point, 4> m_next_repeat{};

  std::vector<Page> m_pages;
  std::vector<Row> m_rows;
  std::string m_status;
  std::optional<Confirmation> m_confirmation;
  std::vector<Disc> m_discs;
  std::vector<std::string> m_post_shaders;
  FiguresSwitch::Figures m_figures;
  FigureTarget m_figure_target;
  std::vector<FiguresSwitch::FigureFile> m_figure_files;
  std::vector<FiguresSwitch::Character> m_figure_characters;
  CheatsSwitch::GameCheats m_cheats;
  bool m_cheats_dirty = false;
  std::function<void()> m_deferred;
  std::function<void()> m_after_resume;
  bool m_config_dirty = false;

  std::mutex m_view_mutex;
  View m_view;

  bool m_pl_initialized = false;
  bool m_have_shared_fonts = false;
  PlFontData m_standard_font{};
  PlFontData m_extended_font{};
  ImGuiContext* m_font_context = nullptr;
  ImFont* m_font = nullptr;
};
