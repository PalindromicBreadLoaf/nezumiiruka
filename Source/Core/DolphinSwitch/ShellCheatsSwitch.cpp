// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellCheatsSwitch.h"

#include <algorithm>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <fmt/format.h>

#include "Common/Thread.h"
#include "DolphinSwitch/CheatsSwitch.h"
#include "UICommon/GameFile.h"

namespace Shell
{
namespace
{
constexpr size_t MAX_DETAIL_NOTES = 8;
constexpr size_t MAX_DETAIL_LINES = 10;

std::thread s_download_thread;

void JoinDownload()
{
  if (s_download_thread.joinable())
    s_download_thread.join();
}

void ShowMessage(const std::string& text)
{
  auto* dialog = new brls::Dialog(text);
  dialog->addButton("OK", [] {});
  dialog->open();
}

template <typename Code>
std::string GetName(const Code& code)
{
  return code.name.empty() ? std::string("Unnamed") : code.name;
}

std::string FormatDetails(const std::string& name, const std::string& creator,
                          const std::vector<std::string>& notes,
                          const std::vector<std::string>& lines)
{
  std::string text = name;
  if (!creator.empty())
    text += fmt::format("\nby {}", creator);

  if (!notes.empty())
  {
    text += "\n";
    for (size_t i = 0; i < notes.size() && i < MAX_DETAIL_NOTES; ++i)
      text += "\n" + notes[i];
    if (notes.size() > MAX_DETAIL_NOTES)
      text += "\n...";
  }

  text += "\n";
  for (size_t i = 0; i < lines.size() && i < MAX_DETAIL_LINES; ++i)
    text += "\n" + lines[i];
  if (lines.size() > MAX_DETAIL_LINES)
    text += fmt::format("\n...and {} more lines", lines.size() - MAX_DETAIL_LINES);

  return text;
}

std::string GetCreator(const Gecko::GeckoCode& code)
{
  return code.creator;
}

std::string GetCreator(const ActionReplay::ARCode&)
{
  return {};
}

std::string GetCreator(const PatchEngine::Patch&)
{
  return {};
}

std::vector<std::string> GetNotes(const Gecko::GeckoCode& code)
{
  return code.notes;
}

std::vector<std::string> GetNotes(const ActionReplay::ARCode&)
{
  return {};
}

std::vector<std::string> GetNotes(const PatchEngine::Patch&)
{
  return {};
}

class GameCheatsView : public brls::Box
{
public:
  explicit GameCheatsView(std::shared_ptr<const UICommon::GameFile> game)
      : brls::Box(brls::Axis::COLUMN), m_game(std::move(game)),
        m_cheats(CheatsSwitch::Load(m_game->GetGameID(), m_game->GetRevision()))
  {
    m_download = new brls::DetailCell();
    m_download->setText("Download Gecko codes");
    m_download->setDetailText("codes.rc24.xyz");
    m_download->registerClickAction([this](brls::View*) {
      Download();
      return true;
    });
    addView(m_download);
    AddNote("Codes already in the list are kept as they are.");

    m_disable_all = new brls::DetailCell();
    m_disable_all->setText("Switch every cheat off");
    m_disable_all->registerClickAction([this](brls::View*) {
      DisableAll();
      return true;
    });
    addView(m_disable_all);

    m_list = new brls::Box(brls::Axis::COLUMN);
    addView(m_list);

    Rebuild();
  }

  ~GameCheatsView() override { *m_alive = false; }

  GameCheatsView(const GameCheatsView&) = delete;
  GameCheatsView& operator=(const GameCheatsView&) = delete;

private:
  void AddNote(const std::string& text, brls::Box* container = nullptr)
  {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(16);
    label->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    label->setVerticalAlign(brls::VerticalAlign::TOP);
    label->setMargins(8, 16, 16, 16);
    (container ? container : this)->addView(label);
  }

  void AddHeader(const std::string& title, size_t count)
  {
    auto* header = new brls::Header();
    header->setTitle(title);
    header->setSubtitle(count == 1 ? std::string("1 code") : fmt::format("{} codes", count));
    header->setMarginTop(24);
    m_list->addView(header);
  }

  void UpdateSummary()
  {
    const size_t enabled = m_cheats.CountEnabled();
    m_disable_all->setDetailText(enabled == 1 ? std::string("1 on") :
                                                fmt::format("{} on", enabled));
  }

  bool IsFocusInList() const
  {
    for (brls::View* view = brls::Application::getCurrentFocus(); view; view = view->getParent())
    {
      if (view == m_list)
        return true;
    }
    return false;
  }

  void Rebuild(std::optional<size_t> focus_cell = std::nullopt)
  {
    if (IsFocusInList())
      brls::Application::giveFocus(m_download);

    m_list->clearViews();
    m_cells.clear();

    AddHeader("Gecko codes", m_cheats.gecko.size());
    if (m_cheats.gecko.empty())
      AddNote("None yet. Download them above or add them to the game's INI by hand.", m_list);
    AddCells(m_cheats.gecko);

    if (!m_cheats.action_replay.empty())
    {
      AddHeader("Action Replay codes", m_cheats.action_replay.size());
      AddCells(m_cheats.action_replay);
    }

    if (!m_cheats.patches.empty())
    {
      AddHeader("Patches", m_cheats.patches.size());
      AddCells(m_cheats.patches);
    }

    UpdateSummary();

    if (focus_cell && !m_cells.empty())
      brls::Application::giveFocus(m_cells[std::min(*focus_cell, m_cells.size() - 1)]);
  }

  template <typename Code>
  void AddCells(std::vector<Code>& codes)
  {
    for (size_t i = 0; i < codes.size(); ++i)
    {
      const size_t cell_index = m_cells.size();

      auto* cell = new brls::BooleanCell();
      cell->init(GetName(codes[i]), codes[i].enabled, [this, &codes, i](bool on) {
        codes[i].enabled = on;
        Save();
      });
      cell->registerAction("Details", brls::BUTTON_X, [this, &codes, i, cell_index](brls::View*) {
        ShowDetails(codes, i, cell_index);
        return true;
      });
      m_list->addView(cell);
      m_cells.push_back(cell);
    }
  }

  template <typename Code>
  void ShowDetails(std::vector<Code>& codes, size_t index, size_t cell_index)
  {
    const Code& code = codes[index];
    auto* dialog = new brls::Dialog(FormatDetails(GetName(code), GetCreator(code), GetNotes(code),
                                                  CheatsSwitch::DescribeLines(code)));
    dialog->addButton("Close", [] {});

    if (code.user_defined)
    {
      dialog->addButton("Delete", [this, alive = m_alive, &codes, index, cell_index] {
        brls::sync([this, alive, &codes, index, cell_index] {
          if (!*alive || index >= codes.size())
            return;
          codes.erase(codes.begin() + index);
          Save();
          Rebuild(cell_index);
        });
      });
    }

    dialog->open();
  }

  void Save()
  {
    CheatsSwitch::Save(m_game->GetGameID(), m_cheats);
    UpdateSummary();
  }

  void DisableAll()
  {
    if (m_cheats.CountEnabled() == 0)
      return;

    const auto disable = [](auto& codes) {
      for (auto& code : codes)
        code.enabled = false;
    };
    disable(m_cheats.gecko);
    disable(m_cheats.action_replay);
    disable(m_cheats.patches);
    Save();
    Rebuild();
  }

  void Download()
  {
    JoinDownload();

    auto* label = new brls::Label();
    label->setText("Downloading Gecko codes...");
    label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    label->setMargins(32, 32, 32, 32);

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->setFocusable(true);
    content->setHideHighlight(true);
    content->addView(label);

    auto* dialog = new brls::Dialog(content);
    dialog->setCancelable(false);
    dialog->open();

    s_download_thread = std::thread(
        [this, alive = m_alive, dialog, gametdb_id = m_game->GetGameTDBID(), cheats = m_cheats] {
          Common::SetCurrentThreadName("Gecko code download");
          CheatsSwitch::GameCheats updated = cheats;
          auto result = CheatsSwitch::DownloadGeckoCodes(gametdb_id, updated);

          brls::sync([this, alive, dialog, result = std::move(result),
                      updated = std::move(updated)]() mutable {
            dialog->close(
                [this, alive, result = std::move(result), updated = std::move(updated)]() mutable {
                  brls::sync([this, alive, result = std::move(result),
                              updated = std::move(updated)]() mutable {
                    if (!result)
                    {
                      ShowMessage(result.error());
                      return;
                    }

                    if (*alive && result->added > 0)
                    {
                      m_cheats.gecko = std::move(updated.gecko);
                      Save();
                      Rebuild();
                    }

                    if (result->added == 0)
                    {
                      ShowMessage(fmt::format("Downloaded {} codes. All were already present.",
                                              result->downloaded));
                    }
                    else
                    {
                      ShowMessage(fmt::format("Downloaded {} codes, {} of them new.",
                                              result->downloaded, result->added));
                    }
                  });
                });
          });
        });
  }

  std::shared_ptr<const UICommon::GameFile> m_game;
  CheatsSwitch::GameCheats m_cheats;
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);

  brls::DetailCell* m_download = nullptr;
  brls::DetailCell* m_disable_all = nullptr;
  brls::Box* m_list = nullptr;
  std::vector<brls::View*> m_cells;
};
}  // namespace

brls::View* CreateGameCheatsView(std::shared_ptr<const UICommon::GameFile> game)
{
  return new GameCheatsView(std::move(game));
}

void StopCheatDownloads()
{
  JoinDownload();
}
}  // namespace Shell
