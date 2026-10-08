// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/PauseMenuSwitch.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <iterator>
#include <tuple>
#include <utility>

#include <fmt/format.h>
#include <imgui.h>

#include "AudioCommon/AudioCommon.h"
#include "Common/HorizonClocks.h"
#include "Core/AchievementManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/HW/DVD/DVDInterface.h"
#include "Core/HW/GBAPad.h"
#include "Core/HW/GCPad.h"
#include "Core/HW/ProcessorInterface.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/HorizonSampler.h"
#include "Core/PatchEngine.h"
#include "Core/State.h"
#include "Core/System.h"
#include "DolphinSwitch/PerformanceOverlaySwitch.h"
#ifdef HAS_LIBMGBA
#include "DolphinSwitch/GBAOverlaySwitch.h"
#endif
#include "DolphinSwitch/SettingsSwitch.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"
#include "InputCommon/InputConfig.h"
#include "UICommon/GameFile.h"
#include "UICommon/GameFileCache.h"
#include "VideoCommon/AsyncRequests.h"
#include "VideoCommon/OnScreenUI.h"
#include "VideoCommon/Present.h"
#include "VideoCommon/VideoConfig.h"

#ifdef HAS_FRAME_GENERATION
#include "VideoBackends/Deko3D/DKFrameGenerationShaders.h"
#endif

namespace
{
using Clock = std::chrono::steady_clock;
using Kind = ControllerProfiles::Kind;

constexpr u64 BUTTON_UP = HidNpadButton_Up | HidNpadButton_StickLUp;
constexpr u64 BUTTON_DOWN = HidNpadButton_Down | HidNpadButton_StickLDown;
constexpr u64 BUTTON_LEFT = HidNpadButton_Left | HidNpadButton_StickLLeft;
constexpr u64 BUTTON_RIGHT = HidNpadButton_Right | HidNpadButton_StickLRight;
constexpr std::array<u64, 4> DIRECTIONS = {BUTTON_UP, BUTTON_DOWN, BUTTON_LEFT, BUTTON_RIGHT};

constexpr u64 OPEN_CHORD = HidNpadButton_Plus | HidNpadButton_Minus;
constexpr u64 CLOSE_BUTTONS = HidNpadButton_Plus | HidNpadButton_Minus;

constexpr u64 STICK_DIRECTIONS = HidNpadButton_StickLLeft | HidNpadButton_StickLUp |
                                 HidNpadButton_StickLRight | HidNpadButton_StickLDown |
                                 HidNpadButton_StickRLeft | HidNpadButton_StickRUp |
                                 HidNpadButton_StickRRight | HidNpadButton_StickRDown;

constexpr auto MINUS_HOLD_TO_OPEN = std::chrono::milliseconds(500);
constexpr auto REPEAT_DELAY = std::chrono::milliseconds(400);
constexpr auto REPEAT_INTERVAL = std::chrono::milliseconds(75);

constexpr int MAX_VISIBLE_ROWS = 10;
constexpr double PROFILE_CAPTURE_SECONDS = 10.0;

constexpr float PANEL_WIDTH = 660.0f;
constexpr float PANEL_PADDING = 32.0f;
constexpr float PANEL_ROUNDING = 18.0f;
constexpr float TITLE_SIZE = 30.0f;
constexpr float PAGE_TITLE_SIZE = 20.0f;
constexpr float MESSAGE_SIZE = 21.0f;
constexpr float ROW_HEIGHT = 46.0f;
constexpr float ROW_TEXT_SIZE = 23.0f;
constexpr float ROW_INSET = 12.0f;
constexpr float HEADER_HEIGHT = 38.0f;
constexpr float HEADER_TEXT_SIZE = 18.0f;
constexpr float FOOTER_SIZE = 20.0f;
constexpr float ARROW_SIZE = 7.0f;

constexpr ImU32 BACKDROP_COLOUR = IM_COL32(0, 0, 0, 150);
constexpr ImU32 PANEL_COLOUR = IM_COL32(30, 32, 38, 245);
constexpr ImU32 PANEL_BORDER_COLOUR = IM_COL32(255, 255, 255, 24);
constexpr ImU32 DIVIDER_COLOUR = IM_COL32(255, 255, 255, 36);
constexpr ImU32 ACCENT_COLOUR = IM_COL32(0, 195, 227, 255);
constexpr ImU32 SELECTED_COLOUR = IM_COL32(0, 195, 227, 48);
constexpr ImU32 TEXT_COLOUR = IM_COL32(240, 240, 244, 255);
constexpr ImU32 DIM_TEXT_COLOUR = IM_COL32(160, 162, 172, 255);
constexpr ImU32 DISABLED_TEXT_COLOUR = IM_COL32(105, 107, 116, 255);

constexpr std::array<const char*, 12> MONTHS = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

std::string FormatSlotTime(u64 unix_ms)
{
  TimeCalendarTime calendar{};
  TimeCalendarAdditionalInfo info{};
  if (R_FAILED(timeToCalendarTimeWithMyRule(unix_ms / 1000, &calendar, &info)))
    return "Saved";

  return fmt::format("{} {} {}, {:02}:{:02}", calendar.day, MONTHS[(calendar.month + 11) % 12],
                     calendar.year, calendar.hour, calendar.minute);
}

float TextWidth(ImFont* font, float size, const std::string& text)
{
  return font->CalcTextSizeA(size, FLT_MAX, 0.0f, text.c_str()).x;
}

std::string Truncate(ImFont* font, float size, std::string text, float max_width)
{
  if (TextWidth(font, size, text) <= max_width)
    return text;

  const float ellipsis_width = TextWidth(font, size, "…");
  while (!text.empty())
  {
    while (!text.empty() && (static_cast<u8>(text.back()) & 0xC0) == 0x80)
      text.pop_back();
    if (!text.empty())
      text.pop_back();
    if (TextWidth(font, size, text) + ellipsis_width <= max_width)
      break;
  }
  return text + "…";
}

enum class ArrowDirection
{
  Up,
  Down,
  Left,
  Right,
};

void DrawArrow(ImDrawList* draw, ImVec2 centre, float size, ArrowDirection direction, ImU32 colour)
{
  switch (direction)
  {
  case ArrowDirection::Up:
    draw->AddTriangleFilled({centre.x - size, centre.y + size * 0.6f},
                            {centre.x + size, centre.y + size * 0.6f},
                            {centre.x, centre.y - size * 0.6f}, colour);
    break;
  case ArrowDirection::Down:
    draw->AddTriangleFilled({centre.x - size, centre.y - size * 0.6f},
                            {centre.x, centre.y + size * 0.6f},
                            {centre.x + size, centre.y - size * 0.6f}, colour);
    break;
  case ArrowDirection::Left:
    draw->AddTriangleFilled({centre.x + size * 0.6f, centre.y - size},
                            {centre.x + size * 0.6f, centre.y + size},
                            {centre.x - size * 0.6f, centre.y}, colour);
    break;
  case ArrowDirection::Right:
    draw->AddTriangleFilled({centre.x - size * 0.6f, centre.y - size},
                            {centre.x + size * 0.6f, centre.y},
                            {centre.x - size * 0.6f, centre.y + size}, colour);
    break;
  }
}
std::string_view ProductModelName(SetSysProductModel model)
{
  switch (model)
  {
  case SetSysProductModel_Nx:
    return "Erista";
  case SetSysProductModel_Iowa:
    return "Mariko";
  case SetSysProductModel_Hoag:
    return "Lite";
  case SetSysProductModel_Aula:
    return "OLED";
  default:
    return "unknown";
  }
}

std::string_view PerformanceProfileName(Config::PerformanceProfile profile)
{
  switch (profile)
  {
  case Config::PerformanceProfile::Stock:
    return "stock";
  case Config::PerformanceProfile::FasterMemoryAndGpu:
    return "faster memory and GPU";
  case Config::PerformanceProfile::FasterMemory:
  default:
    return "faster memory";
  }
}

std::string ProfileClockRate(PcvModule module, PcvModuleId module_id)
{
  u32 hz = 0;
  if (hosversionAtLeast(8, 0, 0))
  {
    ClkrstSession session;
    if (R_FAILED(clkrstOpenSession(&session, module_id, 3)))
      return "n/a";
    const Result rc = clkrstGetClockRate(&session, &hz);
    clkrstCloseSession(&session);
    if (R_FAILED(rc))
      return "n/a";
  }
  else if (R_FAILED(pcvGetClockRate(module, &hz)))
  {
    return "n/a";
  }
  return fmt::format("{:.1f} MHz", hz / 1e6);
}

std::string DescribeHostForProfile()
{
  std::string out = fmt::format("Nezumiiruka {}\n", NEZUMIIRUKA_VERSION);

  SetSysProductModel model = SetSysProductModel_Invalid;
  if (R_SUCCEEDED(setsysInitialize()))
  {
    setsysGetProductModel(&model);
    setsysExit();
  }
  const u32 version = hosversionGet();
  out += fmt::format(
      "console {}, firmware {}.{}.{}{}, {}\n", ProductModelName(model), HOSVER_MAJOR(version),
      HOSVER_MINOR(version), HOSVER_MICRO(version), hosversionIsAtmosphere() ? " (Atmosphere)" : "",
      appletGetOperationMode() == AppletOperationMode_Console ? "docked" : "handheld");

  const bool clkrst = hosversionAtLeast(8, 0, 0);
  const bool clocks_open = R_SUCCEEDED(clkrst ? clkrstInitialize() : pcvInitialize());
  out +=
      fmt::format("clocks: CPU {}, GPU {}, memory {}; clock profile {} (configuration {:#010x})\n",
                  clocks_open ? ProfileClockRate(PcvModule_CpuBus, PcvModuleId_CpuBus) : "n/a",
                  clocks_open ? ProfileClockRate(PcvModule_GPU, PcvModuleId_GPU) : "n/a",
                  clocks_open ? ProfileClockRate(PcvModule_EMC, PcvModuleId_EMC) : "n/a",
                  PerformanceProfileName(Config::Get(Config::SWITCH_PERFORMANCE_PROFILE)),
                  SwitchSettings::GetPerformanceConfiguration());
  if (clocks_open)
  {
    if (clkrst)
      clkrstExit();
    else
      pcvExit();
  }
  return out;
}
}  // namespace

PauseMenu::PauseMenu(Core::System& system, std::string disc_path,
                     std::function<void()> request_quit)
    : m_system(system), m_disc_path(std::move(disc_path)), m_request_quit(std::move(request_quit))
{
  m_pl_initialized = R_SUCCEEDED(plInitialize(PlServiceType_User));
  if (m_pl_initialized)
  {
    m_have_shared_fonts =
        R_SUCCEEDED(plGetSharedFontByType(&m_standard_font, PlSharedFontType_Standard)) &&
        R_SUCCEEDED(plGetSharedFontByType(&m_extended_font, PlSharedFontType_NintendoExt));
  }

  VideoCommon::OnScreenUI::SetHostUICallback([this] {
#ifdef HAS_LIBMGBA
    GBAOverlay::Draw();
#endif
    Draw();
  });
}

PauseMenu::~PauseMenu()
{
  VideoCommon::OnScreenUI::SetHostUICallback({});

  if (m_pl_initialized)
    plExit();
}

void PauseMenu::Update(u64 buttons)
{
  const u64 pressed = buttons & ~m_previous_buttons;
  m_previous_buttons = buttons;

  switch (m_state)
  {
  case State::Closed:
    if (!(buttons & HidNpadButton_Minus))
      m_minus_pressed_at.reset();
    else if (pressed & HidNpadButton_Minus)
      m_minus_pressed_at = Clock::now();

    if (((buttons & OPEN_CHORD) == OPEN_CHORD && (pressed & OPEN_CHORD)) ||
        (m_minus_pressed_at && Clock::now() - *m_minus_pressed_at >= MINUS_HOLD_TO_OPEN))
    {
      m_minus_pressed_at.reset();
      TryOpen();
      if (m_state == State::Open)
        Redraw();
    }
    break;

  case State::Open:
    if (m_deferred)
      std::exchange(m_deferred, {})();
    else
      HandleInput(buttons, pressed);

    if (m_state == State::Open)
      Redraw();
    break;

  case State::Closing:
    if ((buttons & ~STICK_DIRECTIONS) == 0)
      FinishClose();
    break;
  }
}

void PauseMenu::TryOpen()
{
  if (Core::GetState(m_system) != Core::State::Running)
    return;

  Core::SetState(m_system, Core::State::Paused);
  if (Core::GetState(m_system) != Core::State::Paused)
    return;

  m_state = State::Open;
  m_blocking_input.store(true, std::memory_order_relaxed);

  m_pages = {Page{PageId::Main}};
  m_status.clear();
  m_confirmation.reset();
  m_deferred = {};
  m_next_repeat.fill(Clock::now() + REPEAT_DELAY);

  Rebuild();
}

void PauseMenu::Close(std::function<void()> after_resume)
{
  ApplyCheats();
  m_after_resume = std::move(after_resume);
  m_state = State::Closing;
  Publish();
  Redraw();
}

void PauseMenu::FinishClose()
{
  m_state = State::Closed;
  m_pages.clear();
  m_rows.clear();
  m_confirmation.reset();

  if (m_config_dirty)
  {
    Config::Save();
    m_config_dirty = false;
  }

  Core::SetState(m_system, Core::State::Running);
  m_blocking_input.store(false, std::memory_order_relaxed);

  if (const auto after_resume = std::exchange(m_after_resume, {}))
    after_resume();
}

void PauseMenu::HandleInput(u64 buttons, u64 pressed)
{
  if (pressed & CLOSE_BUTTONS)
  {
    Close();
    return;
  }

  if (pressed & HidNpadButton_B)
  {
    Back();
    return;
  }

  const int selected = CurrentPage().selected;
  if ((pressed & HidNpadButton_A) && selected < static_cast<int>(m_rows.size()))
  {
    const Row& row = m_rows[selected];
    if (!row.enabled)
      return;

    if (row.kind == RowKind::Choice)
    {
      ChangeSelection(1);
    }
    else if (row.activate)
    {
      m_status.clear();
      const auto activate = row.activate;
      activate();
    }
    return;
  }

  for (size_t direction = 0; direction < DIRECTIONS.size(); ++direction)
  {
    if (!TakeRepeat(direction, buttons & DIRECTIONS[direction], pressed & DIRECTIONS[direction]))
      continue;

    if (DIRECTIONS[direction] == BUTTON_UP)
      MoveSelection(-1);
    else if (DIRECTIONS[direction] == BUTTON_DOWN)
      MoveSelection(1);
    else if (DIRECTIONS[direction] == BUTTON_LEFT)
      ChangeSelection(-1);
    else
      ChangeSelection(1);
  }
}

bool PauseMenu::TakeRepeat(size_t direction, bool held, bool pressed)
{
  const auto now = Clock::now();
  if (pressed)
  {
    m_next_repeat[direction] = now + REPEAT_DELAY;
    return true;
  }

  if (held && now >= m_next_repeat[direction])
  {
    m_next_repeat[direction] = now + REPEAT_INTERVAL;
    return true;
  }

  return false;
}

void PauseMenu::MoveSelection(int delta)
{
  const int count = static_cast<int>(m_rows.size());
  if (count == 0)
    return;

  Page& page = CurrentPage();
  int index = page.selected;
  for (int i = 0; i < count; ++i)
  {
    index = (index + delta + count) % count;
    if (m_rows[index].kind != RowKind::Header)
      break;
  }

  page.selected = index;
  ClampSelection();
  Publish();
}

void PauseMenu::ChangeSelection(int delta)
{
  const int selected = CurrentPage().selected;
  if (selected >= static_cast<int>(m_rows.size()))
    return;

  const Row& row = m_rows[selected];
  if (!row.enabled || !row.change)
    return;

  m_status.clear();
  const auto change = row.change;
  change(delta);
  Rebuild();
}

void PauseMenu::Push(PageId id)
{
  m_pages.push_back(Page{id});
  m_status.clear();
  Rebuild();
}

void PauseMenu::Back()
{
  if (m_pages.size() <= 1)
  {
    Close();
    return;
  }

  if (CurrentPage().id == PageId::Confirm)
    m_confirmation.reset();
  else if (CurrentPage().id == PageId::Cheats)
    ApplyCheats();

  m_pages.pop_back();
  m_status.clear();
  Rebuild();
}

void PauseMenu::Confirm(std::string question, std::string action, std::function<void()> on_confirm)
{
  m_confirmation = Confirmation{std::move(question), std::move(action), std::move(on_confirm)};
  Push(PageId::Confirm);
}

void PauseMenu::Defer(std::string status, std::function<void()> action)
{
  m_status = std::move(status);
  m_deferred = std::move(action);
  Publish();
}

void PauseMenu::Rebuild()
{
  switch (CurrentPage().id)
  {
  case PageId::Main:
    m_rows = BuildMain();
    break;
  case PageId::SaveState:
    m_rows = BuildSlots(true);
    break;
  case PageId::LoadState:
    m_rows = BuildSlots(false);
    break;
  case PageId::Settings:
    m_rows = BuildSettings();
    break;
  case PageId::Controls:
    m_rows = BuildControls();
    break;
  case PageId::Cheats:
    m_rows = BuildCheats();
    break;
  case PageId::Discs:
    m_rows = BuildDiscs();
    break;
  case PageId::Confirm:
    m_rows = BuildConfirm();
    break;
  }

  ClampSelection();
  Publish();
}

void PauseMenu::ClampSelection()
{
  Page& page = CurrentPage();
  const int count = static_cast<int>(m_rows.size());
  page.selected = std::clamp(page.selected, 0, std::max(count - 1, 0));

  while (page.selected < count - 1 && m_rows[page.selected].kind == RowKind::Header)
    ++page.selected;

  if (page.selected < page.first_visible)
    page.first_visible = page.selected;
  if (page.selected >= page.first_visible + MAX_VISIBLE_ROWS)
    page.first_visible = page.selected - MAX_VISIBLE_ROWS + 1;

  if (page.first_visible > 0 && m_rows[page.first_visible - 1].kind == RowKind::Header &&
      page.selected - page.first_visible + 1 < MAX_VISIBLE_ROWS)
  {
    --page.first_visible;
  }

  page.first_visible = std::clamp(page.first_visible, 0, std::max(count - MAX_VISIBLE_ROWS, 0));
}

std::string PauseMenu::GetPageTitle() const
{
  switch (m_pages.back().id)
  {
  case PageId::Main:
    return "Paused";
  case PageId::SaveState:
    return "Save state";
  case PageId::LoadState:
    return "Load state";
  case PageId::Settings:
    return "Settings";
  case PageId::Controls:
    return "Controls";
  case PageId::Cheats:
    return "Cheats";
  case PageId::Discs:
    return "Change disc";
  case PageId::Confirm:
    return m_confirmation ? m_confirmation->action : "";
  }
  return "";
}

std::vector<PauseMenu::Row> PauseMenu::BuildMain()
{
  std::vector<Row> rows;

  rows.push_back(Row{.label = "Resume", .activate = [this] { Close(); }});
  rows.push_back(Row{.kind = RowKind::Submenu, .label = "Save state", .activate = [this] {
                       Push(PageId::SaveState);
                     }});

  Row load{.kind = RowKind::Submenu, .label = "Load state", .activate = [this] {
             Push(PageId::LoadState);
           }};
  if (AchievementManager::GetInstance().IsHardcoreModeActive())
  {
    load.value = "Not in hardcore mode";
    load.enabled = false;
  }
  rows.push_back(std::move(load));

  rows.push_back(Row{.kind = RowKind::Submenu, .label = "Settings", .activate = [this] {
                       Push(PageId::Settings);
                     }});
  rows.push_back(Row{.kind = RowKind::Submenu, .label = "Controls", .activate = [this] {
                       Push(PageId::Controls);
                     }});

  Row cheats{.kind = RowKind::Submenu, .label = "Cheats", .activate = [this] {
               const SConfig& config = SConfig::GetInstance();
               m_cheats = CheatsSwitch::Load(config.GetGameID(), config.GetRevision());
               Push(PageId::Cheats);
             }};
  if (!Config::AreCheatsEnabled())
  {
    cheats.value = "Off for this game";
    cheats.enabled = false;
  }
  rows.push_back(std::move(cheats));

  if (!m_disc_path.empty())
  {
    rows.push_back(Row{.kind = RowKind::Submenu, .label = "Change disc", .activate = [this] {
                         Defer("Looking for the other discs...", [this] {
                           FindDiscs();
                           Push(PageId::Discs);
                         });
                       }});
  }

  rows.push_back(Row{.label = "Take screenshot",
                     .activate = [this] { Close([] { Core::SaveScreenShot(); }); }});

  rows.push_back(
      Row{.label =
              Core::HorizonSampler::IsRunning() ? "Stop profile capture" : "Capture profile",
          .activate = [this] {
            Close([this] {
              if (Core::HorizonSampler::IsRunning())
                Core::HorizonSampler::Stop();
              else
                Core::HorizonSampler::Start(m_system, PROFILE_CAPTURE_SECONDS,
                                            DescribeHostForProfile());
            });
          }});

  rows.push_back(
      Row{.label = "Reset", .activate = [this] {
            Confirm("Reset the game? Anything you have not saved will be lost.", "Reset", [this] {
              Close([this] { m_system.GetProcessorInterface().ResetButton_Tap(); });
            });
          }});

  rows.push_back(Row{.label = "Quit to game list", .activate = [this] {
                       Confirm("Quit to the game list? Anything you have not saved will be lost.",
                               "Quit", [this] { Close(m_request_quit); });
                     }});

  return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildSlots(bool save)
{
  std::vector<Row> rows;

  for (u32 slot = 1; slot <= ::State::NUM_STATES; ++slot)
  {
    const u64 saved_at = ::State::GetUnixTimeOfSlot(slot);

    Row row{.label = fmt::format("Slot {}", slot),
            .value = saved_at != 0 ? FormatSlotTime(saved_at) : "Empty"};

    if (save)
    {
      const auto save_to_slot = [this, slot] {
        Defer(fmt::format("Saving to slot {}…", slot), [this, slot] {
          RunStateJob([this, slot] { ::State::Save(m_system, slot); });
          m_pages.resize(1);
          m_confirmation.reset();
          Rebuild();
          m_status = fmt::format("Saved to slot {}.", slot);
          Publish();
        });
      };

      if (saved_at == 0)
      {
        row.activate = save_to_slot;
      }
      else
      {
        row.activate = [this, slot, when = row.value, save_to_slot] {
          Confirm(fmt::format("Overwrite slot {}? It was saved on {}.", slot, when), "Overwrite",
                  save_to_slot);
        };
      }
    }
    else
    {
      row.enabled = saved_at != 0;
      row.activate = [this, slot] {
        Defer(fmt::format("Loading slot {}...", slot), [this, slot] {
          RunStateJob([this, slot] { ::State::Load(m_system, slot); });
          Close();
        });
      };
    }

    rows.push_back(std::move(row));
  }

  if (save)
    return rows;

  rows.push_back(Row{.kind = RowKind::Header, .label = "Undo"});

  const bool can_undo_load = ::State::CanUndoLoadState();
  rows.push_back(Row{.label = "Undo last load",
                     .value = can_undo_load ? "" : "Nothing to undo",
                     .enabled = can_undo_load,
                     .activate = [this] {
                       Defer("Undoing the last load...", [this] {
                         RunStateJob([this] { ::State::UndoLoadState(m_system); });
                         Close();
                       });
                     }});

  const u64 overwritten_at = ::State::GetUnixTimeOfUndoSaveState();
  rows.push_back(Row{.label = "Load overwritten state",
                     .value = overwritten_at != 0 ? FormatSlotTime(overwritten_at) : "None",
                     .enabled = overwritten_at != 0,
                     .activate = [this] {
                       Defer("Loading the overwritten state...", [this] {
                         RunStateJob([this] { ::State::UndoSaveState(m_system); });
                         Close();
                       });
                     }});

  return rows;
}

template <typename T>
void PauseMenu::AddChoice(std::vector<Row>& rows, std::string label, Options<T> options,
                          const T& current, std::function<void(const T&)> write)
{
  const auto found = std::ranges::find(options, current, &std::pair<std::string, T>::second);
  const int index = found != options.end() ? static_cast<int>(found - options.begin()) : -1;

  Row row{.kind = RowKind::Choice,
          .label = std::move(label),
          .value = index >= 0 ? options[index].first : "Custom"};
  row.change = [options = std::move(options), index, write = std::move(write)](int delta) {
    const int count = static_cast<int>(options.size());
    const int next = index < 0 ? 0 : (index + delta + count) % count;
    write(options[next].second);
  };
  rows.push_back(std::move(row));
}

std::vector<PauseMenu::Row> PauseMenu::BuildSettings()
{
  std::vector<Row> rows;

  rows.push_back(Row{.kind = RowKind::Header, .label = "This game"});

  AddChoice<int>(rows, "Internal resolution",
                 {{"Native (640×528)", 1},
                  {"2× (1280×1056)", 2},
                  {"3× (1920×1584)", 3},
                  {"4× (2560×2112)", 4}},
                 Config::Get(Config::GFX_EFB_SCALE),
                 [this](const int& value) { WriteForGame(Config::GFX_EFB_SCALE, value); });

  AddChoice<AspectMode>(rows, "Aspect ratio",
                        {{"Auto", AspectMode::Auto},
                         {"Force 16:9", AspectMode::ForceWide},
                         {"Force 4:3", AspectMode::ForceStandard},
                         {"Stretch to screen", AspectMode::Stretch}},
                        Config::Get(Config::GFX_ASPECT_RATIO), [this](const AspectMode& value) {
                          WriteForGame(Config::GFX_ASPECT_RATIO, value);
                        });

  AddChoice<bool>(rows, "Widescreen hack", {{"Off", false}, {"On", true}},
                  Config::Get(Config::GFX_WIDESCREEN_HACK),
                  [this](const bool& value) { WriteForGame(Config::GFX_WIDESCREEN_HACK, value); });

  AddChoice<float>(rows, "Speed limit",
                   {{"50%", 0.5f},
                    {"75%", 0.75f},
                    {"100% (full speed)", 1.0f},
                    {"125%", 1.25f},
                    {"150%", 1.5f},
                    {"200%", 2.0f},
                    {"Unlimited", 0.0f}},
                   Config::Get(Config::MAIN_EMULATION_SPEED), [this](const float& value) {
                     WriteForGame(Config::MAIN_EMULATION_SPEED, value);
                   });

#ifdef HAS_FRAME_GENERATION
  if (Config::Get(Config::MAIN_GFX_BACKEND) == "Deko3D")
  {
    if (Deko3D::FrameGeneration::GetShaderStatus() ==
        Deko3D::FrameGeneration::ShaderStatus::Prepared)
    {
      const u32 multiplier = Config::Get(Config::GFX_FRAME_GENERATION) ?
                                 Config::Get(Config::GFX_FRAME_GENERATION_MULTIPLIER) :
                                 0;
      AddChoice<u32>(rows, "Frame generation", {{"Off", 0}, {"2×", 2}, {"3×", 3}, {"4×", 4}},
                     multiplier, [this](const u32& value) {
                       WriteForGame(Config::GFX_FRAME_GENERATION, value != 0);
                       if (value != 0)
                         WriteForGame(Config::GFX_FRAME_GENERATION_MULTIPLIER, value);
                     });
    }
    else
    {
      rows.push_back(Row{.kind = RowKind::Choice,
                         .label = "Frame generation",
                         .value = "Prepare it from the game list",
                         .enabled = false});
    }
  }
#endif

  rows.push_back(Row{.kind = RowKind::Header, .label = "All games"});

  AddChoice<int>(rows, "Volume",
                 {{"Muted", 0},
                  {"10%", 10},
                  {"20%", 20},
                  {"30%", 30},
                  {"40%", 40},
                  {"50%", 50},
                  {"60%", 60},
                  {"70%", 70},
                  {"80%", 80},
                  {"90%", 90},
                  {"100%", 100}},
                 Config::Get(Config::MAIN_AUDIO_VOLUME), [this](const int& value) {
                   WriteGlobal(Config::MAIN_AUDIO_VOLUME, value);
                   AudioCommon::UpdateSoundStream(m_system);
                 });

  AddChoice<int>(rows, "Performance overlay",
                 {{"Off", 0}, {"Statistics", 1}, {"Statistics and graphs", 2}},
                 Config::Get(Config::SWITCH_PERFORMANCE_OVERLAY), [this](const int& value) {
                   PerfOverlay::SetLevel(value);
                   m_config_dirty = true;
                 });

  AddChoice<Config::PerformanceProfile>(
      rows, "Clock profile",
      {{"Stock (memory 1331 MHz)", Config::PerformanceProfile::Stock},
       {"Faster memory (1600 MHz)", Config::PerformanceProfile::FasterMemory},
       {"Faster memory and GPU (460 MHz)", Config::PerformanceProfile::FasterMemoryAndGpu}},
      Config::Get(Config::SWITCH_PERFORMANCE_PROFILE),
      [this](const Config::PerformanceProfile& value) {
        WriteGlobal(Config::SWITCH_PERFORMANCE_PROFILE, value);
        Common::HorizonClocks::ApplyPerformanceConfiguration(
            SwitchSettings::GetPerformanceConfiguration());
      });

  return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildControls()
{
  std::vector<Row> rows;

  if (m_system.IsWii())
  {
    std::vector<Row> remotes;
    for (int index = 0; index < ControllerProfiles::SLOT_COUNT; ++index)
    {
      if (Config::Get(Config::GetInfoForWiimoteSource(index)) == WiimoteSource::Emulated)
        remotes.push_back(
            ProfileRow(Kind::Wiimote, index, fmt::format("Wii Remote {}", index + 1)));
    }

    if (!remotes.empty())
    {
      rows.push_back(Row{.kind = RowKind::Header, .label = "Wii Remotes"});
      std::ranges::move(remotes, std::back_inserter(rows));
    }
  }

  std::vector<Row> pads;
  for (int port = 0; port < ControllerProfiles::SLOT_COUNT; ++port)
  {
    if (Config::Get(Config::GetInfoForSIDevice(port)) == SerialInterface::SIDEVICE_GC_CONTROLLER)
      pads.push_back(ProfileRow(Kind::GCPad, port, fmt::format("Port {}", port + 1)));
  }

  if (!pads.empty())
  {
    rows.push_back(Row{.kind = RowKind::Header, .label = "GameCube controllers"});
    std::ranges::move(pads, std::back_inserter(rows));
  }

#ifdef HAS_LIBMGBA
  std::vector<Row> gbas;
  for (int port = 0; port < ControllerProfiles::SLOT_COUNT; ++port)
  {
    if (Config::Get(Config::GetInfoForSIDevice(port)) != SerialInterface::SIDEVICE_GC_GBA_EMULATED)
      continue;

    gbas.push_back(Row{.label = fmt::format("Reset GBA {}", port + 1), .activate = [this, port] {
                         Pad::SetGBAReset(port, true);
                         Close();
                       }});
  }

  if (!gbas.empty())
  {
    rows.push_back(Row{.kind = RowKind::Header, .label = "Game Boy Advance"});
    AddChoice<Config::GBAScreens>(rows, "Screens",
                                  {{"Hidden", Config::GBAScreens::Hidden},
                                   {"Small", Config::GBAScreens::Small},
                                   {"Large", Config::GBAScreens::Large}},
                                  Config::Get(Config::SWITCH_GBA_SCREENS),
                                  [this](const Config::GBAScreens& value) {
                                    WriteGlobal(Config::SWITCH_GBA_SCREENS, value);
                                  });
    AddChoice<Config::GBAScreenCorner>(rows, "Position",
                                       {{"Top left", Config::GBAScreenCorner::TopLeft},
                                        {"Top right", Config::GBAScreenCorner::TopRight},
                                        {"Bottom left", Config::GBAScreenCorner::BottomLeft},
                                        {"Bottom right", Config::GBAScreenCorner::BottomRight}},
                                       Config::Get(Config::SWITCH_GBA_SCREEN_CORNER),
                                       [this](const Config::GBAScreenCorner& value) {
                                         WriteGlobal(Config::SWITCH_GBA_SCREEN_CORNER, value);
                                       });
    std::ranges::move(gbas, std::back_inserter(rows));
  }
#endif

  if (rows.empty())
    rows.push_back(Row{.label = "No emulated controllers are connected.", .enabled = false});

  return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildCheats()
{
  std::vector<Row> rows;

  const auto add_section = [this, &rows](std::string header, auto& codes) {
    if (codes.empty())
      return;

    rows.push_back(Row{.kind = RowKind::Header, .label = std::move(header)});
    for (size_t i = 0; i < codes.size(); ++i)
    {
      Row row{.kind = RowKind::Choice,
              .label = codes[i].name.empty() ? std::string("Unnamed") : codes[i].name,
              .value = codes[i].enabled ? "On" : "Off"};
      row.change = [this, &codes, i](int) {
        codes[i].enabled = !codes[i].enabled;
        CheatsSwitch::Save(SConfig::GetInstance().GetGameID(), m_cheats);
        m_cheats_dirty = true;
      };
      rows.push_back(std::move(row));
    }
  };

  add_section("Gecko codes", m_cheats.gecko);
  add_section("Action Replay codes", m_cheats.action_replay);
  add_section("Patches", m_cheats.patches);

  if (rows.empty())
  {
    rows.push_back(
        Row{.label = "No cheats yet. Download them from the game list.", .enabled = false});
  }

  return rows;
}

void PauseMenu::ApplyCheats()
{
  if (!std::exchange(m_cheats_dirty, false))
    return;

  RunStateJob([this] {
    Core::RunOnCPUThread(m_system, [&system = m_system] { PatchEngine::Reload(system); });
  });
}

PauseMenu::Row PauseMenu::ProfileRow(Kind kind, int slot, std::string label)
{
  std::vector<std::string> names = ControllerProfiles::GetPresetNames(kind);
  std::ranges::move(ControllerProfiles::GetUserProfileNames(kind), std::back_inserter(names));

  std::string current = Config::Get(ControllerProfiles::GetGameProfileInfo(kind, slot));
  if (current.empty())
    current = ControllerProfiles::GetGlobalProfile(kind, slot);

  const auto found = std::ranges::find(names, current);
  const int index = found != names.end() ? static_cast<int>(found - names.begin()) : -1;

  Row row{.kind = RowKind::Choice,
          .label = std::move(label),
          .value = current.empty() ? "None" : current,
          .enabled = !names.empty()};
  row.change = [this, kind, slot, names = std::move(names), index](int delta) {
    const int count = static_cast<int>(names.size());
    const int next = index < 0 ? 0 : (index + delta + count) % count;
    ApplyProfile(kind, slot, names[next]);
  };
  return row;
}

void PauseMenu::ApplyProfile(Kind kind, int slot, const std::string& name)
{
  InputConfig* const config = kind == Kind::GCPad ? Pad::GetConfig() : Wiimote::GetConfig();
  ControllerEmu::EmulatedController* const controller = config->GetController(slot);

  if (!ControllerProfiles::Load(kind, name, *controller))
  {
    m_status = fmt::format("Could not read the profile \"{}\".", name);
    return;
  }
  controller->UpdateReferences(g_controller_interface);

  WriteForGame(ControllerProfiles::GetGameProfileInfo(kind, slot), name);
}

void PauseMenu::FindDiscs()
{
  m_discs.clear();

  const std::string game_id = SConfig::GetInstance().GetGameID();

  UICommon::GameFileCache cache;
  cache.Load();
  cache.ForEach([this, &game_id](const std::shared_ptr<const UICommon::GameFile>& game) {
    if (game->GetGameID() == game_id && game->GetFilePath() != m_disc_path)
      m_discs.push_back(Disc{game->GetDiscNumber() + 1, game->GetFilePath(), game->GetFileName()});
  });

  std::ranges::sort(m_discs, {},
                    [](const Disc& disc) { return std::tie(disc.number, disc.file_name); });
}

std::vector<PauseMenu::Row> PauseMenu::BuildDiscs()
{
  std::vector<Row> rows;

  for (const Disc& disc : m_discs)
  {
    rows.push_back(Row{.label = fmt::format("Disc {}", disc.number),
                       .value = disc.file_name,
                       .activate = [this, path = disc.path] {
                         {
                           const Core::CPUThreadGuard guard(m_system);
                           m_system.GetDVDInterface().ChangeDisc(guard, path);
                         }
                         m_disc_path = path;
                         Close();
                       }});
  }

  if (rows.empty())
    rows.push_back(
        Row{.label = "No other discs of this game are in the game list.", .enabled = false});

  return rows;
}

std::vector<PauseMenu::Row> PauseMenu::BuildConfirm()
{
  if (!m_confirmation)
    return {};

  return {
      Row{.label = m_confirmation->action,
          .activate =
              [this] {
                const auto on_confirm = m_confirmation->on_confirm;
                on_confirm();
              }},
      Row{.label = "Cancel", .activate = [this] { Back(); }},
  };
}

template <typename T>
void PauseMenu::WriteForGame(const Config::Info<T>& info, const T& value)
{
  if (Config::GetLayer(Config::LayerType::LocalGame))
    Config::Set(Config::LayerType::LocalGame, info, value);

  if (Config::Get(info) != value)
    Config::SetCurrent(info, value);

  m_config_dirty = true;
}

template <typename T>
void PauseMenu::WriteGlobal(const Config::Info<T>& info, const T& value)
{
  Config::SetBase(info, value);
  if (Config::Get(info) != value)
    Config::SetCurrent(info, value);

  m_config_dirty = true;
}

void PauseMenu::RunStateJob(const std::function<void()>& job)
{
  m_state_job_pending.store(true, std::memory_order_relaxed);
  job();
  Core::RunOnCPUThread(m_system,
                       [this] { m_state_job_pending.store(false, std::memory_order_release); });
}

void PauseMenu::Publish()
{
  View view;
  view.visible = m_state == State::Open;

  if (view.visible)
  {
    const SConfig& config = SConfig::GetInstance();
    view.title = config.GetTitleName();
    if (view.title.empty())
      view.title = config.GetGameID();

    view.page_title = GetPageTitle();
    view.status = m_status;

    const PageId page = CurrentPage().id;
    if (page == PageId::Confirm && m_confirmation)
      view.message = m_confirmation->question;
    else if (page == PageId::Settings)
      view.message = "Changes take effect straight away.";
    else if (page == PageId::Controls)
      view.message = "Profiles are created and edited from the game list.";
    else if (page == PageId::Cheats)
      view.message = "Changes take effect when you leave this page.";

    view.rows.reserve(m_rows.size());
    for (const Row& row : m_rows)
      view.rows.push_back(ViewRow{row.kind, row.label, row.value, row.enabled});

    view.selected = CurrentPage().selected;
    view.first_visible = CurrentPage().first_visible;
  }

  std::lock_guard lock(m_view_mutex);
  m_view = std::move(view);
}

void PauseMenu::Redraw()
{
  if (m_state_job_pending.load(std::memory_order_acquire) ||
      Core::GetState(m_system) != Core::State::Paused)
  {
    return;
  }

  const Core::CPUThreadGuard guard(m_system);
  AsyncRequests::GetInstance()->PushEvent([] {
    if (g_presenter)
      g_presenter->Present();
  });
}

ImFont* PauseMenu::GetFont()
{
  ImGuiContext* const context = ImGui::GetCurrentContext();
  if (context != m_font_context)
  {
    m_font_context = context;
    m_font = nullptr;

    if (m_have_shared_fonts)
    {
      ImFontAtlas* const atlas = ImGui::GetIO().Fonts;

      ImFontConfig font_config;
      font_config.FontDataOwnedByAtlas = false;
      m_font = atlas->AddFontFromMemoryTTF(
          m_standard_font.address, static_cast<int>(m_standard_font.size), 0.0f, &font_config);
      if (m_font)
      {
        font_config.MergeMode = true;
        atlas->AddFontFromMemoryTTF(m_extended_font.address, static_cast<int>(m_extended_font.size),
                                    0.0f, &font_config);
      }
    }
  }

  return m_font ? m_font : ImGui::GetFont();
}

void PauseMenu::Draw()
{
  std::lock_guard lock(m_view_mutex);
  if (!m_view.visible)
    return;

  ImFont* const font = GetFont();
  ImDrawList* const draw = ImGui::GetForegroundDrawList();
  const ImVec2 display = ImGui::GetIO().DisplaySize;
  const float scale = display.y / 720.0f;

  const float width = PANEL_WIDTH * scale;
  const float padding = PANEL_PADDING * scale;
  const float inner_width = width - padding * 2.0f;
  const float row_height = ROW_HEIGHT * scale;
  const float header_height = HEADER_HEIGHT * scale;
  const float row_text = ROW_TEXT_SIZE * scale;
  const float message_size = MESSAGE_SIZE * scale;
  const float footer_size = FOOTER_SIZE * scale;
  const float arrow = ARROW_SIZE * scale;

  const int row_count = static_cast<int>(m_view.rows.size());
  const int first = std::clamp(m_view.first_visible, 0, std::max(row_count - 1, 0));
  const int last = std::min(first + MAX_VISIBLE_ROWS, row_count);

  float list_height = 0.0f;
  for (int i = first; i < last; ++i)
    list_height += m_view.rows[i].kind == RowKind::Header ? header_height : row_height;

  float message_height = 0.0f;
  if (!m_view.message.empty())
  {
    message_height =
        font->CalcTextSizeA(message_size, FLT_MAX, inner_width, m_view.message.c_str()).y +
        14.0f * scale;
  }

  const float heading_height =
      (TITLE_SIZE + 4.0f + PAGE_TITLE_SIZE + 16.0f) * scale + 12.0f * scale;
  const float footer_height = (12.0f + 14.0f) * scale + footer_size;
  const float height =
      padding * 2.0f + heading_height + message_height + list_height + footer_height;

  const ImVec2 panel_min{std::floor((display.x - width) * 0.5f),
                         std::floor((display.y - height) * 0.5f)};
  const ImVec2 panel_max{panel_min.x + width, panel_min.y + height};
  const float left = panel_min.x + padding;
  const float right = panel_max.x - padding;

  draw->AddRectFilled({0.0f, 0.0f}, display, BACKDROP_COLOUR);
  draw->AddRectFilled(panel_min, panel_max, PANEL_COLOUR, PANEL_ROUNDING * scale);
  draw->AddRect(panel_min, panel_max, PANEL_BORDER_COLOUR, PANEL_ROUNDING * scale, 0, scale);

  float y = panel_min.y + padding;

  const std::string title = Truncate(font, TITLE_SIZE * scale, m_view.title, inner_width);
  draw->AddText(font, TITLE_SIZE * scale, {left, y}, TEXT_COLOUR, title.c_str());
  y += (TITLE_SIZE + 4.0f) * scale;

  draw->AddText(font, PAGE_TITLE_SIZE * scale, {left, y}, ACCENT_COLOUR, m_view.page_title.c_str());
  y += (PAGE_TITLE_SIZE + 16.0f) * scale;

  draw->AddLine({left, y}, {right, y}, DIVIDER_COLOUR, scale);
  y += 12.0f * scale;

  if (!m_view.message.empty())
  {
    draw->AddText(font, message_size, {left, y}, DIM_TEXT_COLOUR, m_view.message.c_str(), nullptr,
                  inner_width);
    y += message_height;
  }

  const float list_top = y;
  for (int i = first; i < last; ++i)
  {
    const ViewRow& row = m_view.rows[i];

    if (row.kind == RowKind::Header)
    {
      const float text_y = y + header_height - HEADER_TEXT_SIZE * scale - 8.0f * scale;
      draw->AddText(font, HEADER_TEXT_SIZE * scale, {left, text_y}, DIM_TEXT_COLOUR,
                    row.label.c_str());
      y += header_height;
      continue;
    }

    const bool selected = i == m_view.selected;
    const ImVec2 row_min{panel_min.x + ROW_INSET * scale, y};
    const ImVec2 row_max{panel_max.x - ROW_INSET * scale, y + row_height};
    if (selected)
    {
      draw->AddRectFilled(row_min, row_max, SELECTED_COLOUR, 10.0f * scale);
      draw->AddRect(row_min, row_max, ACCENT_COLOUR, 10.0f * scale, 0, 2.0f * scale);
    }

    const float text_y = y + (row_height - row_text) * 0.5f;
    const float centre_y = y + row_height * 0.5f;
    const ImU32 label_colour = row.enabled ? TEXT_COLOUR : DISABLED_TEXT_COLOUR;
    const ImU32 value_colour =
        !row.enabled ? DISABLED_TEXT_COLOUR : (selected ? TEXT_COLOUR : DIM_TEXT_COLOUR);

    float value_right = right;
    if (row.kind == RowKind::Submenu)
    {
      DrawArrow(draw, {right - arrow * 0.6f, centre_y}, arrow, ArrowDirection::Right, label_colour);
      value_right -= arrow * 3.0f;
    }
    const bool arrows = row.kind == RowKind::Choice && selected && row.enabled;
    if (arrows)
    {
      DrawArrow(draw, {right - arrow * 0.6f, centre_y}, arrow, ArrowDirection::Right,
                ACCENT_COLOUR);
      value_right -= arrow * 3.0f;
    }

    const float label_width = TextWidth(font, row_text, row.label);
    float value_width = 0.0f;
    if (!row.value.empty())
    {
      const float space =
          value_right - left - label_width - 32.0f * scale - (arrows ? arrow * 3.0f : 0.0f);
      const std::string value = Truncate(font, row_text, row.value, std::max(space, 0.0f));
      value_width = TextWidth(font, row_text, value);
      draw->AddText(font, row_text, {value_right - value_width, text_y}, value_colour,
                    value.c_str());
    }
    if (arrows)
    {
      DrawArrow(draw, {value_right - value_width - arrow * 2.0f, centre_y}, arrow,
                ArrowDirection::Left, ACCENT_COLOUR);
    }

    const std::string label = Truncate(font, row_text, row.label, inner_width);
    draw->AddText(font, row_text, {left, text_y}, label_colour, label.c_str());

    y += row_height;
  }

  if (first > 0)
    DrawArrow(draw, {right, list_top - 6.0f * scale}, arrow * 0.8f, ArrowDirection::Up,
              DIM_TEXT_COLOUR);
  if (last < row_count)
    DrawArrow(draw, {right, y + 6.0f * scale}, arrow * 0.8f, ArrowDirection::Down, DIM_TEXT_COLOUR);

  y += 12.0f * scale;
  draw->AddLine({left, y}, {right, y}, DIVIDER_COLOUR, scale);
  y += 14.0f * scale;

  const std::string hints =
      m_font ? " Select     Back     Resume" : "A Select    B Back    + Resume";
  const float hints_width = TextWidth(font, footer_size, hints);
  draw->AddText(font, footer_size, {right - hints_width, y}, DIM_TEXT_COLOUR, hints.c_str());

  if (!m_view.status.empty())
  {
    const std::string status =
        Truncate(font, footer_size, m_view.status, inner_width - hints_width - 24.0f * scale);
    draw->AddText(font, footer_size, {left, y}, ACCENT_COLOUR, status.c_str());
  }
}
