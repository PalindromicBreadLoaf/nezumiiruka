// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellGameListSwitch.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <utility>

#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/dropdown.hpp>
#include <borealis/views/hint.hpp>
#include <fmt/format.h>
#include <nanovg.h>

#include "Common/StringUtil.h"
#include "Core/CommonTitles.h"
#include "Core/Config/UISettings.h"
#include "DiscIO/Enums.h"
#include "DolphinSwitch/ShellFormatSwitch.h"
#include "DolphinSwitch/ShellSettingsSwitch.h"
#include "DolphinSwitch/ShellSystemSwitch.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"

namespace Shell
{
namespace
{
constexpr float DETAILS_WIDTH = 420;
constexpr float DETAILS_PADDING = 30;
constexpr float COVER_HEIGHT = 220;
constexpr size_t ROWS_PER_PAGE = 10;

constexpr std::array<const char*, 3> FILTER_NAMES = {"All", "GameCube", "Wii"};

GameListView* s_instance = nullptr;

bool MatchesFilter(const UICommon::GameFile& game, Config::GameListFilter filter)
{
  switch (filter)
  {
  case Config::GameListFilter::GameCube:
    return game.GetPlatform() == DiscIO::Platform::GameCubeDisc ||
           game.GetPlatform() == DiscIO::Platform::Triforce;
  case Config::GameListFilter::Wii:
    return DiscIO::IsWii(game.GetPlatform());
  case Config::GameListFilter::All:
  default:
    return true;
  }
}

std::string ToLower(std::string text)
{
  Common::ToLower(&text);
  return text;
}

NVGcolor ThemeColour(const std::string& name)
{
  brls::Theme theme = brls::Application::getTheme();
  return theme[name];
}
}  // namespace

class GameCell : public brls::DetailCell
{
public:
  GameCell(GamePtr game, std::function<void(GameCell*)> on_focus)
      : m_game(std::move(game)), m_on_focus(std::move(on_focus))
  {
    setText(GetTitle(*m_game));
    setDetailText(GetShortPlatformName(*m_game));
    setDetailTextColor(ThemeColour("brls/text_disabled"));
  }

  void onFocusGained() override
  {
    brls::DetailCell::onFocusGained();
    m_on_focus(this);
  }

  const GamePtr& GetGame() const { return m_game; }
  void SetGame(GamePtr game) { m_game = std::move(game); }

private:
  GamePtr m_game;
  std::function<void(GameCell*)> m_on_focus;
};

GameListView::GameListView(Library& library, BootRequest& chosen, const std::string& focus_path)
    : brls::Box(brls::Axis::ROW), m_library(library), m_chosen(chosen), m_focused_path(focus_path),
      m_game_directory(SwitchSettings::GetGameDirectory()),
      m_filter(Config::Get(Config::SWITCH_GAME_LIST_FILTER)),
      m_sort(Config::Get(Config::SWITCH_GAME_LIST_SORT)),
      m_use_covers(Config::Get(Config::MAIN_USE_GAME_COVERS)), m_games(library.GetGames())
{
  s_instance = this;
  setGrow(1);

  auto* left = new brls::Box(brls::Axis::COLUMN);
  left->setGrow(1);
  addView(left);

  m_filter_bar = new brls::Box(brls::Axis::ROW);
  m_filter_bar->setPadding(16, 40, 8, 40);
  m_filter_bar->setAlignItems(brls::AlignItems::CENTER);
  left->addView(m_filter_bar);

  m_list = new brls::Box(brls::Axis::COLUMN);
  m_list->setPadding(8, 40, 32, 40);
  m_scroll = new brls::ScrollingFrame();
  m_scroll->setGrow(1);
  m_scroll->setContentView(m_list);
  left->addView(m_scroll);

  m_details = new brls::Box(brls::Axis::COLUMN);
  m_details->setWidth(DETAILS_WIDTH);
  m_details->setPadding(DETAILS_PADDING);
  m_details->setLineLeft(1);
  m_details->setLineColor(ThemeColour("brls/applet_frame/separator"));
  addView(m_details);

  auto* cover_box = new brls::Box(brls::Axis::COLUMN);
  cover_box->setDimensions(DETAILS_WIDTH - 2 * DETAILS_PADDING, COVER_HEIGHT);
  cover_box->setJustifyContent(brls::JustifyContent::CENTER);
  cover_box->setAlignItems(brls::AlignItems::CENTER);
  cover_box->setMarginBottom(20);
  m_details->addView(cover_box);

  m_cover = new brls::Image();
  m_cover->setDimensions(DETAILS_WIDTH - 2 * DETAILS_PADDING, COVER_HEIGHT);
  m_cover->setScalingType(brls::ImageScalingType::FIT);
  m_cover->setClipsToBounds(false);
  cover_box->addView(m_cover);

  m_cover_placeholder = new brls::Label();
  m_cover_placeholder->setText("No cover");
  m_cover_placeholder->setTextColor(ThemeColour("brls/text_disabled"));
  cover_box->addView(m_cover_placeholder);

  m_title = new brls::Label();
  m_title->setFontSize(24);
  m_title->setWidth(DETAILS_WIDTH - 2 * DETAILS_PADDING);
  m_title->setVerticalAlign(brls::VerticalAlign::TOP);
  m_details->addView(m_title);

  m_maker = new brls::Label();
  m_maker->setFontSize(16);
  m_maker->setWidth(DETAILS_WIDTH - 2 * DETAILS_PADDING);
  m_maker->setVerticalAlign(brls::VerticalAlign::TOP);
  m_maker->setMarginTop(4);
  m_maker->setTextColor(ThemeColour("brls/header/subtitle"));
  m_maker->setMarginBottom(16);
  m_details->addView(m_maker);

  AddDetailRow(m_details, "Platform", &m_platform);
  AddDetailRow(m_details, "Region", &m_region);
  AddDetailRow(m_details, "Game ID", &m_game_id);
  m_title_id_row = AddDetailRow(m_details, "Title ID", &m_title_id);
  AddDetailRow(m_details, "Played", &m_time);
  AddDetailRow(m_details, "File", &m_file);
  AddDetailRow(m_details, "Size", &m_size);
  AddDetailRow(m_details, "Format", &m_format);

  registerAction("Settings", brls::BUTTON_BACK, [this](brls::View*) {
    OpenSettings();
    return true;
  });
  registerAction("Options", brls::BUTTON_X, [this](brls::View*) {
    OpenProperties();
    return true;
  });
  registerAction("Jump to", brls::BUTTON_Y, [this](brls::View*) {
    OpenJumpMenu();
    return true;
  });
  registerAction("Wii Menu", brls::BUTTON_RSB, [this](brls::View*) {
    LaunchSystemMenu();
    return true;
  });
  registerAction(
      "Previous", brls::BUTTON_LB,
      [this](brls::View*) {
        JumpSection(-1);
        return true;
      },
      true, true);
  registerAction(
      "Next", brls::BUTTON_RB,
      [this](brls::View*) {
        JumpSection(1);
        return true;
      },
      true, true);
  registerAction(
      "Filter", brls::BUTTON_RT,
      [this](brls::View*) {
        CycleFilter(1);
        return true;
      },
      true);
  registerAction(
      "Filter", brls::BUTTON_LT,
      [this](brls::View*) {
        CycleFilter(-1);
        return true;
      },
      true);

  m_run_loop_subscription = brls::Application::getRunLoopEvent()->subscribe([this] {
    if (!m_rebuild_pending)
      return;

    const std::vector<brls::Activity*> stack = brls::Application::getActivitiesStack();
    if (!stack.empty() && stack.back() == getParentActivity())
      Rebuild();
  });

  Rebuild();
  StartRefresh();
}

GameListView::~GameListView()
{
  brls::Application::getRunLoopEvent()->unsubscribe(m_run_loop_subscription);
  m_library.Stop();
  if (s_instance == this)
    s_instance = nullptr;
}

brls::Box* GameListView::AddDetailRow(brls::Box* container, const std::string& key,
                                      brls::Label** value)
{
  auto* row = new brls::Box(brls::Axis::ROW);
  row->setMarginBottom(6);

  auto* key_label = new brls::Label();
  key_label->setText(key);
  key_label->setFontSize(16);
  key_label->setWidth(90);
  key_label->setTextColor(ThemeColour("brls/text_disabled"));
  row->addView(key_label);

  *value = new brls::Label();
  (*value)->setFontSize(16);
  (*value)->setSingleLine(true);
  (*value)->setGrow(1);
  (*value)->setShrink(1);
  row->addView(*value);

  container->addView(row);
  return row;
}

void GameListView::StartRefresh()
{
  m_library.Refresh(
      [this](Games games) {
        brls::sync([this, games = std::move(games)]() mutable { SetGames(std::move(games)); });
      },
      [this](GamePtr game) { brls::sync([this, game = std::move(game)] { UpdateGame(game); }); });
}

void GameListView::SetGames(Games games)
{
  m_games = std::move(games);
  m_rebuild_pending = true;
}

void GameListView::UpdateGame(const GamePtr& game)
{
  const std::string& path = game->GetFilePath();

  for (GamePtr& entry : m_games)
  {
    if (entry->GetFilePath() == path)
      entry = game;
  }
  for (size_t row = 0; row < m_visible.size(); ++row)
  {
    if (m_visible[row]->GetFilePath() != path)
      continue;
    m_visible[row] = game;
    m_cells[row]->SetGame(game);
  }

  if (m_focused_path == path)
    ShowDetails(game);
}

std::chrono::milliseconds GameListView::GetTimePlayed(const UICommon::GameFile& game) const
{
  if (game.GetGameID().empty())
    return std::chrono::milliseconds::zero();
  return m_time_played.GetTimePlayed(game.GetGameID());
}

std::string GameListView::GetSectionLabel(const UICommon::GameFile& game) const
{
  const std::string key =
      m_sort == Config::GameListSort::FileName ? game.GetFileName() : GetTitle(game);
  if (key.empty() || !std::isalpha(static_cast<unsigned char>(key[0])))
    return "#";
  return std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key[0]))));
}

void GameListView::Rebuild()
{
  m_rebuild_pending = false;

  m_visible.clear();
  for (const GamePtr& game : m_games)
  {
    if (MatchesFilter(*game, m_filter))
      m_visible.push_back(game);
  }

  switch (m_sort)
  {
  case Config::GameListSort::TimePlayed:
  {
    std::ranges::stable_sort(m_visible, [this](const GamePtr& a, const GamePtr& b) {
      const auto time_a = GetTimePlayed(*a);
      const auto time_b = GetTimePlayed(*b);
      if (time_a != time_b)
        return time_a > time_b;
      return ToLower(GetTitle(*a)) < ToLower(GetTitle(*b));
    });
    break;
  }
  case Config::GameListSort::FileName:
    std::ranges::stable_sort(m_visible, [](const GamePtr& a, const GamePtr& b) {
      return ToLower(a->GetFileName()) < ToLower(b->GetFileName());
    });
    break;
  case Config::GameListSort::Title:
  default:
    std::ranges::stable_sort(m_visible, [](const GamePtr& a, const GamePtr& b) {
      return ToLower(GetTitle(*a)) < ToLower(GetTitle(*b));
    });
    break;
  }

  m_list->clearViews();
  m_cells.clear();
  m_sections.clear();

  RebuildFilterBar();

  if (m_visible.empty())
  {
    auto* empty = new brls::DetailCell();
    empty->setText(m_games.empty() ? "No games found" : "No games match this filter");
    empty->setDetailText(m_games.empty() ? m_game_directory : "ZL / ZR to change");
    empty->registerClickAction([this](brls::View*) {
      OpenSettings();
      return true;
    });
    m_list->addView(empty);
    m_details->setVisibility(brls::Visibility::INVISIBLE);
    brls::Application::giveFocus(empty);
    return;
  }

  m_details->setVisibility(brls::Visibility::VISIBLE);

  size_t focus_row = 0;
  for (size_t row = 0; row < m_visible.size(); ++row)
  {
    const GamePtr& game = m_visible[row];

    auto* cell = new GameCell(game, [this](GameCell* focused_cell) {
      m_focused_path = focused_cell->GetGame()->GetFilePath();
      ShowDetails(focused_cell->GetGame());
    });
    cell->registerClickAction([this, cell](brls::View*) {
      Launch(cell->GetGame());
      return true;
    });
    m_list->addView(cell);
    m_cells.push_back(cell);

    if (game->GetFilePath() == m_focused_path)
      focus_row = row;

    if (m_sort != Config::GameListSort::TimePlayed)
    {
      std::string label = GetSectionLabel(*game);
      if (m_sections.empty() || m_sections.back().label != label)
        m_sections.push_back({std::move(label), row});
    }
  }

  FocusRow(focus_row);
}

void GameListView::RebuildFilterBar()
{
  m_filter_bar->clearViews();

  const auto add_button_icon = [this](brls::ControllerButton button) {
    auto* icon = new brls::Label();
    icon->setText(brls::Hint::getKeyIcon(button, true));
    icon->setFontSize(22);
    icon->setMarginRight(20);
    icon->setTextColor(ThemeColour("brls/text_disabled"));
    m_filter_bar->addView(icon);
  };

  add_button_icon(brls::BUTTON_LT);
  for (size_t i = 0; i < FILTER_NAMES.size(); ++i)
  {
    auto* label = new brls::Label();
    label->setText(FILTER_NAMES[i]);
    label->setFontSize(20);
    label->setMarginRight(28);
    const bool active = static_cast<size_t>(m_filter) == i;
    label->setTextColor(active ? ThemeColour("brls/accent") : ThemeColour("brls/text_disabled"));
    m_filter_bar->addView(label);
  }
  add_button_icon(brls::BUTTON_RT);

  auto* spacer = new brls::Box();
  spacer->setGrow(1);
  m_filter_bar->addView(spacer);

  auto* count = new brls::Label();
  count->setFontSize(16);
  count->setTextColor(ThemeColour("brls/text_disabled"));
  count->setText(m_visible.size() == 1 ? std::string("1 game") :
                                         fmt::format("{} games", m_visible.size()));
  m_filter_bar->addView(count);
}

void GameListView::FocusRow(size_t row)
{
  if (row >= m_cells.size())
    return;
  brls::Application::giveFocus(m_cells[row]);
}

void GameListView::ShowDetails(const GamePtr& game)
{
  m_title->setText(GetTitle(*game));
  m_maker->setText(game->GetMaker(UICommon::GameFile::Variant::LongAndPossiblyCustom));
  m_platform->setText(GetPlatformName(*game));
  m_region->setText(GetRegionName(*game));

  std::string game_id = game->GetGameID().empty() ? std::string("—") : game->GetGameID();
  const std::string game_id_detail = GetGameIdDetail(*game);
  if (!game_id_detail.empty())
    game_id += fmt::format(" ({})", game_id_detail);
  m_game_id->setText(game_id);

  const std::string title_id = GetTitleIdText(*game);
  m_title_id->setText(title_id);
  m_title_id_row->setVisibility(title_id.empty() ? brls::Visibility::GONE :
                                                   brls::Visibility::VISIBLE);

  m_time->setText(FormatTimePlayed(GetTimePlayed(*game)));
  m_file->setText(game->GetFileName());
  m_size->setText(UICommon::FormatSize(game->GetFileSize()));
  m_format->setText(GetFormatText(*game));

  ShowCover(*game);
}

void GameListView::ShowCover(const UICommon::GameFile& game)
{
  const UICommon::GameCover& cover = game.GetCoverImage();
  const UICommon::GameBanner& banner = game.GetBannerImage();

  const std::string key =
      fmt::format("{}:{}:{}", game.GetFilePath(), cover.buffer.size(), banner.buffer.size());
  if (key == m_cover_path)
    return;
  m_cover_path = key;

  m_cover->clear();

  NVGcontext* vg = brls::Application::getNVGContext();
  int texture = 0;
  if (!cover.empty())
  {
    texture = nvgCreateImageMem(vg, 0, const_cast<unsigned char*>(cover.buffer.data()),
                                static_cast<int>(cover.buffer.size()));
  }
  else if (!banner.empty())
  {
    std::vector<u8> rgba(banner.buffer.size() * 4);
    for (size_t i = 0; i < banner.buffer.size(); ++i)
    {
      const u32 pixel = banner.buffer[i];
      rgba[i * 4 + 0] = static_cast<u8>(pixel >> 16);
      rgba[i * 4 + 1] = static_cast<u8>(pixel >> 8);
      rgba[i * 4 + 2] = static_cast<u8>(pixel);
      rgba[i * 4 + 3] = 0xFF;
    }
    texture = nvgCreateImageRGBA(vg, static_cast<int>(banner.width),
                                 static_cast<int>(banner.height), 0, rgba.data());
  }

  if (texture != 0)
  {
    m_cover->innerSetImage(texture);
    m_cover->setVisibility(brls::Visibility::VISIBLE);
    m_cover_placeholder->setVisibility(brls::Visibility::GONE);
  }
  else
  {
    m_cover->setVisibility(brls::Visibility::GONE);
    m_cover_placeholder->setVisibility(brls::Visibility::VISIBLE);
  }
}

void GameListView::Launch(const GamePtr& game, bool riivolution)
{
  m_chosen = {.path = game->GetFilePath(), .riivolution = riivolution};
  brls::Application::quit();
}

void GameListView::LaunchSystemMenu()
{
  if (!IsSystemMenuInstalled())
  {
    auto* dialog = new brls::Dialog(
        "The Wii Menu is not installed. Install it with an online system update or by "
        "importing a NAND backup under Settings > Wii system.");
    dialog->addButton("OK", [] {});
    dialog->open();
    return;
  }

  m_chosen = {.nand_title_id = Titles::SYSTEM_MENU};
  brls::Application::quit();
}

void GameListView::OpenProperties()
{
  const auto it = std::ranges::find_if(
      m_visible, [this](const GamePtr& game) { return game->GetFilePath() == m_focused_path; });
  if (it == m_visible.end())
    return;

  GamePtr game = *it;
  brls::Application::pushActivity(CreateGamePropertiesActivity(
      game, GetTimePlayed(*game), [this, game](bool riivolution) { Launch(game, riivolution); }));
}

void GameListView::OpenSettings()
{
  brls::Application::pushActivity(CreateSettingsActivity(
      [] {
        if (s_instance)
          s_instance->OnSettingsClosed();
      },
      [] {
        if (s_instance)
          s_instance->ClearCache();
      },
      [] {
        if (s_instance)
          s_instance->LaunchSystemMenu();
      }));
}

void GameListView::OnSettingsClosed()
{
  m_filter = Config::Get(Config::SWITCH_GAME_LIST_FILTER);
  m_sort = Config::Get(Config::SWITCH_GAME_LIST_SORT);
  m_cover_path.clear();
  m_rebuild_pending = true;

  const std::string directory = SwitchSettings::GetGameDirectory();
  const bool use_covers = Config::Get(Config::MAIN_USE_GAME_COVERS);
  if (directory != m_game_directory || use_covers != m_use_covers)
  {
    m_game_directory = directory;
    m_use_covers = use_covers;
    StartRefresh();
  }
}

void GameListView::ClearCache()
{
  m_library.ClearCache();
  m_games.clear();
  m_cover_path.clear();
  m_rebuild_pending = true;
  StartRefresh();
}

void GameListView::JumpSection(int direction)
{
  if (m_cells.empty())
    return;

  const auto it = std::ranges::find_if(
      m_visible, [this](const GamePtr& game) { return game->GetFilePath() == m_focused_path; });
  const size_t row = it == m_visible.end() ? 0 : static_cast<size_t>(it - m_visible.begin());

  if (m_sections.empty())
  {
    if (direction > 0)
      FocusRow(std::min(row + ROWS_PER_PAGE, m_cells.size() - 1));
    else
      FocusRow(row > ROWS_PER_PAGE ? row - ROWS_PER_PAGE : 0);
    return;
  }

  size_t section = 0;
  while (section + 1 < m_sections.size() && m_sections[section + 1].first_row <= row)
    ++section;

  if (direction > 0)
  {
    if (section + 1 < m_sections.size())
      FocusRow(m_sections[section + 1].first_row);
    else
      FocusRow(m_cells.size() - 1);
  }
  else if (row != m_sections[section].first_row || section == 0)
  {
    FocusRow(m_sections[section].first_row);
  }
  else
  {
    FocusRow(m_sections[section - 1].first_row);
  }
}

void GameListView::OpenJumpMenu()
{
  if (m_sections.empty())
  {
    brls::Application::notify("Jumping by letter needs the list sorted by title or file name.");
    return;
  }

  std::vector<std::string> labels;
  for (const Section& section : m_sections)
    labels.push_back(section.label);

  auto target = std::make_shared<int>(-1);
  auto* dropdown = new brls::Dropdown(
      "Jump to", labels, [target](int selected) { *target = selected; }, 0,
      [this, target](int) {
        if (*target >= 0 && static_cast<size_t>(*target) < m_sections.size())
          FocusRow(m_sections[*target].first_row);
      });
  brls::Application::pushActivity(new brls::Activity(dropdown));
}

void GameListView::CycleFilter(int direction)
{
  const int count = static_cast<int>(FILTER_NAMES.size());
  const int next = (static_cast<int>(m_filter) + direction + count) % count;
  m_filter = static_cast<Config::GameListFilter>(next);
  Config::SetBase(Config::SWITCH_GAME_LIST_FILTER, m_filter);
  Rebuild();
}
}  // namespace Shell
