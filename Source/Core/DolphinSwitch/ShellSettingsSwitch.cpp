// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellSettingsSwitch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

#include <switch.h>

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_input.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/Config/Config.h"
#include "Common/Config/Layer.h"
#include "Common/FileUtil.h"
#include "Common/HorizonBuildId.h"
#include "Common/Logging/LogManager.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"
#ifdef USE_RETRO_ACHIEVEMENTS
#include "Core/Config/AchievementSettings.h"
#endif
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Config/SYSCONFSettings.h"
#include "Core/Config/UISettings.h"
#include "Core/Config/WiimoteSettings.h"
#include "Core/ConfigLoaders/GameConfigLoader.h"
#include "Core/HW/EXI/EXI_Device.h"
#include "Core/HW/SI/SI_Device.h"
#include "Core/HW/Wiimote.h"
#include "Core/PowerPC/PowerPC.h"
#include "DiscIO/Enums.h"
#include "DolphinSwitch/ControllerProfilesSwitch.h"
#include "DolphinSwitch/FiguresSwitch.h"
#include "DolphinSwitch/RiivolutionSwitch.h"
#include "DolphinSwitch/SettingsSwitch.h"
#ifdef USE_RETRO_ACHIEVEMENTS
#include "DolphinSwitch/ShellAchievementsSwitch.h"
#endif
#include "DolphinSwitch/ShellCheatsSwitch.h"
#include "DolphinSwitch/ShellControlsSwitch.h"
#include "DolphinSwitch/ShellFilePickerSwitch.h"
#include "DolphinSwitch/ShellFormatSwitch.h"
#include "DolphinSwitch/ShellMemoryCardsSwitch.h"
#include "DolphinSwitch/ShellSystemSwitch.h"
#include "DolphinSwitch/ShellUpdaterSwitch.h"
#include "DolphinSwitch/UpdaterSwitch.h"
#include "DolphinSwitch/UsbStorageSwitch.h"
#include "UICommon/GameFile.h"
#include "UICommon/UICommon.h"
#ifdef HAS_FRAME_GENERATION
#include "VideoBackends/Deko3D/DKFrameGenerationShaders.h"
#endif
#include "VideoCommon/GraphicsModSystem/Config/GraphicsMod.h"
#include "VideoCommon/GraphicsModSystem/Config/GraphicsModGroup.h"
#include "VideoCommon/PostProcessing.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

namespace Shell
{
namespace
{
template <typename T>
struct Option
{
  std::string label;
  T value;
};

template <typename T>
bool SameValue(const T& a, const T& b)
{
  if constexpr (std::is_floating_point_v<T>)
    return std::abs(a - b) < 0.001f;
  else
    return a == b;
}

template <typename T>
int IndexOf(const std::vector<Option<T>>& options, const T& value)
{
  for (size_t i = 0; i < options.size(); ++i)
  {
    if (SameValue(options[i].value, value))
      return static_cast<int>(i);
  }
  return -1;
}

class SettingsContext
{
public:
  explicit SettingsContext(std::function<void()> on_closed) : m_on_closed(std::move(on_closed)) {}

  explicit SettingsContext(const UICommon::GameFile& game)
      : m_game_layer(std::make_unique<Config::Layer>(
            ConfigLoaders::GenerateLocalGameConfigLoader(game.GetGameID(), game.GetRevision()))),
        m_game_defaults(std::make_unique<Config::Layer>(
            ConfigLoaders::GenerateGlobalGameConfigLoader(game.GetGameID(), game.GetRevision()))),
        m_game_ini_path(File::GetUserPath(D_GAMESETTINGS_IDX) + game.GetGameID() + ".ini")
  {
  }

  ~SettingsContext()
  {
    if (m_game_layer)
    {
      m_game_layer.reset();
      if (File::Exists(m_game_ini_path) && File::GetSize(m_game_ini_path) == 0)
        File::Delete(m_game_ini_path);
    }
    else
    {
      Config::Save();
    }

    if (m_on_closed)
      m_on_closed();
  }

  SettingsContext(const SettingsContext&) = delete;
  SettingsContext& operator=(const SettingsContext&) = delete;

  bool IsPerGame() const { return m_game_layer != nullptr; }

  template <typename T>
  T ReadInherited(const Config::Info<T>& info) const
  {
    if (m_game_defaults && m_game_defaults->Exists(info.GetLocation()))
      return m_game_defaults->Get(info);
    return Config::GetBase(info);
  }

  template <typename T>
  T Read(const Config::Info<T>& info) const
  {
    if (IsOverridden(info.GetLocation()))
      return m_game_layer->Get(info);
    return ReadInherited(info);
  }

  template <typename T>
  void Write(const Config::Info<T>& info, const T& value)
  {
    if (m_game_layer)
      m_game_layer->Set(info, value);
    else
      Config::SetBase(info, value);
  }

  bool IsOverridden(const Config::Location& location) const
  {
    return m_game_layer && m_game_layer->Exists(location);
  }

  void Clear(const Config::Location& location)
  {
    if (m_game_layer)
      m_game_layer->DeleteKey(location);
  }

  void ClearAll()
  {
    if (m_game_layer)
      m_game_layer->DeleteAllKeys();
  }

private:
  std::unique_ptr<Config::Layer> m_game_layer;
  std::unique_ptr<Config::Layer> m_game_defaults;
  std::string m_game_ini_path;
  std::function<void()> m_on_closed;
};

using ContextPtr = std::shared_ptr<SettingsContext>;

struct Binding
{
  std::function<int()> current;
  std::function<int()> inherited;
  std::function<void(int)> write;
  std::function<bool()> overridden;
  std::function<void()> clear;
};

class PageBuilder
{
public:
  PageBuilder(ContextPtr context, brls::Box* box) : m_context(std::move(context)), m_box(box) {}

  bool IsPerGame() const { return m_context->IsPerGame(); }
  const ContextPtr& GetContext() const { return m_context; }

  brls::View* Header(const std::string& title, const std::string& subtitle = {})
  {
    auto* header = new brls::Header();
    header->setTitle(title);
    if (!subtitle.empty())
      header->setSubtitle(subtitle);
    header->setMarginTop(m_box->getChildren().empty() ? 0 : 24);
    m_box->addView(header);
    return header;
  }

  brls::View* Note(const std::string& text)
  {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(16);
    label->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    label->setVerticalAlign(brls::VerticalAlign::TOP);
    label->setMargins(8, 16, 16, 16);
    m_box->addView(label);
    return label;
  }

  brls::View* Toggle(const std::string& title, const Config::Info<bool>& info,
                     bool inverted = false, std::function<void()> on_changed = {})
  {
    if (IsPerGame())
      return Choice(title, info, {{"Off", inverted}, {"On", !inverted}}, std::move(on_changed));

    auto* cell = new brls::BooleanCell();
    cell->init(title, m_context->Read(info) != inverted,
               [context = m_context, &info, inverted, on_changed = std::move(on_changed)](bool on) {
                 context->Write(info, on != inverted);
                 if (on_changed)
                   on_changed();
               });
    m_box->addView(cell);
    return cell;
  }

  template <typename T>
  brls::View* Choice(const std::string& title, const Config::Info<T>& info,
                     std::vector<Option<T>> options, std::function<void()> on_changed = {})
  {
    std::vector<std::string> labels;
    for (const Option<T>& option : options)
      labels.push_back(option.label);

    const Config::Location location = info.GetLocation();
    Binding binding;
    binding.current = [context = m_context, &info, options] {
      return IndexOf(options, context->Read(info));
    };
    binding.inherited = [context = m_context, &info, options] {
      return IndexOf(options, context->ReadInherited(info));
    };
    binding.write = [context = m_context, &info, options](int index) {
      context->Write(info, options[index].value);
    };
    binding.overridden = [context = m_context, location] {
      return context->IsOverridden(location);
    };
    binding.clear = [context = m_context, location] { context->Clear(location); };

    return Custom(title, std::move(labels), std::move(binding), std::move(on_changed));
  }

  brls::View* Custom(const std::string& title, std::vector<std::string> labels, Binding binding,
                     std::function<void()> on_changed = {})
  {
    auto* cell = new brls::SelectorCell();
    const int option_count = static_cast<int>(labels.size());

    if (!IsPerGame())
    {
      int selected = binding.current();
      if (selected < 0)
      {
        labels.push_back("Custom");
        selected = option_count;
      }

      cell->init(title, labels, selected, [binding, option_count, on_changed](int index) {
        if (index < option_count)
          binding.write(index);
        if (on_changed)
          on_changed();
      });
      m_box->addView(cell);
      return cell;
    }

    const int inherited = binding.inherited();
    std::vector<std::string> choices;
    choices.push_back(
        fmt::format("Default ({})", inherited >= 0 ? labels[inherited] : std::string("Custom")));
    choices.insert(choices.end(), labels.begin(), labels.end());

    int selected = 0;
    if (binding.overridden())
    {
      const int current = binding.current();
      if (current < 0)
      {
        choices.push_back("Custom");
        selected = static_cast<int>(choices.size()) - 1;
      }
      else
      {
        selected = current + 1;
      }
    }

    const auto colour_detail = [cell](bool overridden) {
      brls::Theme theme = brls::Application::getTheme();
      cell->setDetailTextColor(overridden ? theme["brls/list/listItem_value_color"] :
                                            theme["brls/text_disabled"]);
    };

    cell->init(title, choices, selected,
               [binding, option_count, colour_detail, on_changed](int index) {
                 if (index == 0)
                   binding.clear();
                 else if (index <= option_count)
                   binding.write(index - 1);
                 colour_detail(index != 0);
                 if (on_changed)
                   on_changed();
               });
    colour_detail(selected != 0);
    m_box->addView(cell);
    return cell;
  }

  brls::View* Text(const std::string& title, const Config::Info<std::string>& info,
                   const std::string& placeholder)
  {
    auto* cell = new brls::InputCell();
    cell->init(
        title, m_context->Read(info),
        [context = m_context, &info](std::string value) { context->Write(info, value); },
        placeholder, title, 256);
    m_box->addView(cell);
    return cell;
  }

  brls::DetailCell* Action(const std::string& title, const std::string& detail,
                           std::function<void()> action)
  {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(detail);
    cell->registerClickAction([action = std::move(action)](brls::View*) {
      action();
      return true;
    });
    m_box->addView(cell);
    return cell;
  }

  void Add(brls::View* view) { m_box->addView(view); }

  void Info(const std::string& title, const std::string& value)
  {
    if (value.empty())
      return;

    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(value);
    cell->setDetailTextColor(brls::Application::getTheme()["brls/text"]);
    m_box->addView(cell);
  }

private:
  ContextPtr m_context;
  brls::Box* m_box;
};

class Dependents
{
public:
  explicit Dependents(std::function<bool()> condition) : m_condition(std::move(condition)) {}

  void Add(brls::View* view)
  {
    m_views.push_back(view);
    view->setVisibility(m_condition() ? brls::Visibility::VISIBLE : brls::Visibility::GONE);
  }

  void Refresh() const
  {
    const brls::Visibility visibility =
        m_condition() ? brls::Visibility::VISIBLE : brls::Visibility::GONE;
    for (brls::View* view : m_views)
      view->setVisibility(visibility);
  }

private:
  std::function<bool()> m_condition;
  std::vector<brls::View*> m_views;
};

using DependentsPtr = std::shared_ptr<Dependents>;

DependentsPtr ShowWhen(std::function<bool()> condition)
{
  return std::make_shared<Dependents>(std::move(condition));
}

std::function<void()> RefreshOf(std::initializer_list<DependentsPtr> dependents)
{
  return [dependents = std::vector<DependentsPtr>(dependents)] {
    for (const DependentsPtr& entry : dependents)
      entry->Refresh();
  };
}

using PageFn = std::function<void(PageBuilder&)>;

brls::View* CreatePage(const ContextPtr& context, const PageFn& build, bool is_settings = true)
{
  brls::Style style = brls::Application::getStyle();

  auto* box = new brls::Box(brls::Axis::COLUMN);
  box->setPadding(style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"],
                  style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"]);

  PageBuilder builder(context, box);
  if (context->IsPerGame() && is_settings)
    builder.Note("Anything left on Default follows the global settings.");
  build(builder);

  auto* scroll = new brls::ScrollingFrame();
  scroll->setContentView(box);
  return scroll;
}

void AddPage(brls::TabFrame* tabs, const ContextPtr& context, const std::string& label,
             const PageFn& build)
{
  tabs->addTab(label, [context, build] { return CreatePage(context, build); });
}

Binding OverclockBinding(const ContextPtr& context, const Config::Info<bool>& enable_info,
                         const Config::Info<float>& factor_info, const std::vector<float>& factors)
{
  const auto index_of = [factors](bool enabled, float factor) {
    if (!enabled)
      return 0;
    for (size_t i = 0; i < factors.size(); ++i)
    {
      if (SameValue(factors[i], factor))
        return static_cast<int>(i) + 1;
    }
    return -1;
  };

  Binding binding;
  binding.current = [context, index_of, &enable_info, &factor_info] {
    return index_of(context->Read(enable_info), context->Read(factor_info));
  };
  binding.inherited = [context, index_of, &enable_info, &factor_info] {
    return index_of(context->ReadInherited(enable_info), context->ReadInherited(factor_info));
  };
  binding.write = [context, factors, &enable_info, &factor_info](int index) {
    context->Write(enable_info, index != 0);
    if (index != 0)
      context->Write(factor_info, factors[index - 1]);
  };
  binding.overridden = [context, &enable_info, &factor_info] {
    return context->IsOverridden(enable_info.GetLocation()) ||
           context->IsOverridden(factor_info.GetLocation());
  };
  binding.clear = [context, &enable_info, &factor_info] {
    context->Clear(enable_info.GetLocation());
    context->Clear(factor_info.GetLocation());
  };
  return binding;
}

Binding AntiAliasingBinding(const ContextPtr& context)
{
  const auto index_of = [](u32 samples, bool ssaa) {
    if (samples <= 1)
      return 0;
    const int base = ssaa ? 3 : 1;
    if (samples == 2)
      return base;
    if (samples == 4)
      return base + 1;
    return -1;
  };

  Binding binding;
  binding.current = [context, index_of] {
    return index_of(context->Read(Config::GFX_MSAA), context->Read(Config::GFX_SSAA));
  };
  binding.inherited = [context, index_of] {
    return index_of(context->ReadInherited(Config::GFX_MSAA),
                    context->ReadInherited(Config::GFX_SSAA));
  };
  binding.write = [context](int index) {
    static constexpr std::array<u32, 5> samples = {1, 2, 4, 2, 4};
    context->Write(Config::GFX_MSAA, samples[index]);
    context->Write(Config::GFX_SSAA, index >= 3);
  };
  binding.overridden = [context] {
    return context->IsOverridden(Config::GFX_MSAA.GetLocation()) ||
           context->IsOverridden(Config::GFX_SSAA.GetLocation());
  };
  binding.clear = [context] {
    context->Clear(Config::GFX_MSAA.GetLocation());
    context->Clear(Config::GFX_SSAA.GetLocation());
  };
  return binding;
}

Binding CropBinding(const ContextPtr& context, const Config::Info<int>& first,
                    const Config::Info<int>& second, const std::vector<int>& amounts)
{
  const auto index_of = [amounts](bool enabled, int a, int b) {
    if (!enabled)
      return 0;
    if (a != b)
      return -1;
    for (size_t i = 0; i < amounts.size(); ++i)
    {
      if (amounts[i] == a)
        return static_cast<int>(i);
    }
    return -1;
  };

  Binding binding;
  binding.current = [context, index_of, &first, &second] {
    return index_of(context->Read(Config::GFX_CROP_CUSTOM), context->Read(first),
                    context->Read(second));
  };
  binding.inherited = [context, index_of, &first, &second] {
    return index_of(context->ReadInherited(Config::GFX_CROP_CUSTOM), context->ReadInherited(first),
                    context->ReadInherited(second));
  };
  binding.write = [context, amounts, &first, &second](int index) {
    context->Write(Config::GFX_CROP_CUSTOM, true);
    context->Write(first, amounts[index]);
    context->Write(second, amounts[index]);
  };
  binding.overridden = [context, &first, &second] {
    return context->IsOverridden(first.GetLocation()) ||
           context->IsOverridden(second.GetLocation());
  };
  binding.clear = [context, &first, &second] {
    context->Clear(first.GetLocation());
    context->Clear(second.GetLocation());
    if (!context->IsOverridden(Config::GFX_CROP_CUSTOM_LEFT.GetLocation()) &&
        !context->IsOverridden(Config::GFX_CROP_CUSTOM_TOP.GetLocation()))
    {
      context->Clear(Config::GFX_CROP_CUSTOM.GetLocation());
    }
  };
  return binding;
}

std::string FormatRtc(u32 value)
{
  using namespace std::chrono;
  const sys_seconds time{seconds(value)};
  const sys_days day = floor<days>(time);
  const year_month_day date{day};
  const hh_mm_ss<seconds> clock{time - day};
  return fmt::format("{:04}-{:02}-{:02} {:02}:{:02}", static_cast<int>(date.year()),
                     static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                     clock.hours().count(), clock.minutes().count());
}

std::optional<u32> ParseRtc(const std::string& text)
{
  using namespace std::chrono;
  int year = 0;
  unsigned month = 0, day = 0, hour = 0, minute = 0;
  const int fields =
      std::sscanf(text.c_str(), "%d-%u-%u %u:%u", &year, &month, &day, &hour, &minute);
  if (fields != 3 && fields != 5)
    return std::nullopt;

  const year_month_day date{std::chrono::year(year), std::chrono::month(month),
                            std::chrono::day(day)};
  if (!date.ok() || hour > 23 || minute > 59)
    return std::nullopt;

  const sys_seconds time = sys_days(date) + hours(hour) + minutes(minute);
  const s64 value = time.time_since_epoch().count();
  if (value < 0 || value > std::numeric_limits<u32>::max())
    return std::nullopt;
  return static_cast<u32>(value);
}

brls::View* AddRtcCell(PageBuilder& page)
{
  const ContextPtr context = page.GetContext();

  auto* cell = new brls::InputCell();
  cell->init(
      "Date and time", FormatRtc(context->Read(Config::MAIN_CUSTOM_RTC_VALUE)),
      [context, cell](std::string text) {
        const std::optional<u32> value = ParseRtc(text);
        if (!value)
        {
          brls::Application::notify("Enter the date as YYYY-MM-DD HH:MM.");
          cell->setValue(FormatRtc(context->Read(Config::MAIN_CUSTOM_RTC_VALUE)));
          return;
        }
        context->Write(Config::MAIN_CUSTOM_RTC_VALUE, *value);
        cell->setValue(FormatRtc(*value));
      },
      "YYYY-MM-DD HH:MM", "Date and time (YYYY-MM-DD HH:MM)", 16);
  page.Add(cell);
  return cell;
}

void BuildGeneral(PageBuilder& page)
{
  if (!page.IsPerGame())
  {
    page.Header("Switch");
    page.Choice(
        "Clock profile", Config::SWITCH_PERFORMANCE_PROFILE,
        {{"Stock (memory 1331 MHz)", Config::PerformanceProfile::Stock},
         {"Faster memory (1600 MHz)", Config::PerformanceProfile::FasterMemory},
         {"Faster memory and GPU (460 MHz)", Config::PerformanceProfile::FasterMemoryAndGpu}});
    page.Note("Faster memory helps most games. GPU is almost always unnecessary.");
    page.Choice("Performance overlay", Config::SWITCH_PERFORMANCE_OVERLAY,
                {{"Off", 0}, {"Statistics", 1}, {"Statistics and graphs", 2}});
    page.Note("Press L3 and R3 together in game to cycle the overlay.");
  }

  page.Header("Emulation speed");
  page.Choice("Speed limit", Config::MAIN_EMULATION_SPEED,
              {{"Unlimited", 0.0f},
               {"50%", 0.5f},
               {"75%", 0.75f},
               {"100% (full speed)", 1.0f},
               {"125%", 1.25f},
               {"150%", 1.5f},
               {"200%", 2.0f}});

  page.Header("Interface");
  page.Toggle("Show on-screen messages", Config::MAIN_OSD_MESSAGES);
  if (!page.IsPerGame())
  {
    page.Choice("On-screen text size", Config::MAIN_OSD_FONT_SIZE,
                {{"Small", 13}, {"Medium", 16}, {"Large", 20}, {"Extra large", 24}});
  }
  page.Toggle("Enable cheats", Config::MAIN_ENABLE_CHEATS);
  if (!page.IsPerGame())
    page.Toggle("Track time played", Config::MAIN_TIME_TRACKING);

  page.Header("Discs");
  page.Toggle("Change discs automatically", Config::MAIN_AUTO_DISC_CHANGE);
}

void BuildGraphics(PageBuilder& page)
{
  std::vector<Option<std::string>> backends;
  for (const auto& backend : VideoBackendBase::GetAvailableBackends())
    backends.push_back({backend->GetDisplayName(), backend->GetConfigName()});

  page.Header("Renderer");
  page.Choice("Video backend", Config::MAIN_GFX_BACKEND, std::move(backends));
  page.Toggle("V-Sync", Config::GFX_VSYNC);

  page.Header("Display");
  page.Choice("Internal resolution", Config::GFX_EFB_SCALE,
              {{"Native (640×528)", 1},
               {"2× (1280×1056) for 720p", 2},
               {"3× (1920×1584) for 1080p", 3},
               {"4× (2560×2112)", 4}});
  page.Choice("Aspect ratio", Config::GFX_ASPECT_RATIO,
              {{"Auto", AspectMode::Auto},
               {"Force 16:9", AspectMode::ForceWide},
               {"Force 4:3", AspectMode::ForceStandard},
               {"Stretch to screen", AspectMode::Stretch}});
  page.Toggle("Widescreen hack", Config::GFX_WIDESCREEN_HACK);
  page.Toggle("Crop to aspect ratio", Config::GFX_CROP_TO_ASPECT_RATIO);

  const std::vector<int> crop_amounts = {0, 4, 8, 12, 16, 24, 32};
  std::vector<std::string> crop_labels = {"Off"};
  for (size_t i = 1; i < crop_amounts.size(); ++i)
    crop_labels.push_back(fmt::format("{} px", crop_amounts[i]));
  page.Custom("Crop left and right", crop_labels,
              CropBinding(page.GetContext(), Config::GFX_CROP_CUSTOM_LEFT,
                          Config::GFX_CROP_CUSTOM_RIGHT, crop_amounts));
  page.Custom("Crop top and bottom", crop_labels,
              CropBinding(page.GetContext(), Config::GFX_CROP_CUSTOM_TOP,
                          Config::GFX_CROP_CUSTOM_BOTTOM, crop_amounts));
  page.Note("Hides the black borders some games leave around the picture.");

  page.Choice("Output resampling", Config::GFX_ENHANCE_OUTPUT_RESAMPLING,
              {{"Default", OutputResamplingMode::Default},
               {"Bilinear", OutputResamplingMode::Bilinear},
               {"Bicubic: B-spline", OutputResamplingMode::BSpline},
               {"Bicubic: Mitchell-Netravali", OutputResamplingMode::MitchellNetravali},
               {"Bicubic: Catmull-Rom", OutputResamplingMode::CatmullRom},
               {"Sharp bilinear", OutputResamplingMode::SharpBilinear},
               {"Area sampling", OutputResamplingMode::AreaSampling}});

  page.Header("Shaders");
  page.Choice("Shader compilation", Config::GFX_SHADER_COMPILATION_MODE,
              {{"Specialised (stutters)", ShaderCompilationMode::Synchronous},
               {"Exclusive ubershaders", ShaderCompilationMode::SynchronousUberShaders},
               {"Hybrid ubershaders", ShaderCompilationMode::AsynchronousUberShaders},
               {"Skip drawing until compiled", ShaderCompilationMode::AsynchronousSkipRendering}});
  page.Toggle("Compile shaders before starting", Config::GFX_WAIT_FOR_SHADERS_BEFORE_STARTING);
}

void BuildEnhancements(PageBuilder& page)
{
  page.Header("Texture filtering");
  page.Choice("Anisotropic filtering", Config::GFX_ENHANCE_MAX_ANISOTROPY,
              {{"Game default", AnisotropicFilteringMode::Default},
               {"1×", AnisotropicFilteringMode::Force1x},
               {"2×", AnisotropicFilteringMode::Force2x},
               {"4×", AnisotropicFilteringMode::Force4x},
               {"8×", AnisotropicFilteringMode::Force8x},
               {"16×", AnisotropicFilteringMode::Force16x}});
  page.Choice("Texture filtering", Config::GFX_ENHANCE_FORCE_TEXTURE_FILTERING,
              {{"Game default", TextureFilteringMode::Default},
               {"Force nearest", TextureFilteringMode::Nearest},
               {"Force linear", TextureFilteringMode::Linear}});
  page.Toggle("Arbitrary mipmap detection", Config::GFX_ENHANCE_ARBITRARY_MIPMAP_DETECTION);

  page.Header("Custom textures");
  page.Toggle("Load custom textures", Config::GFX_HIRES_TEXTURES);
  page.Toggle("Prefetch custom textures", Config::GFX_CACHE_HIRES_TEXTURES);
  page.Note(fmt::format("Put each texture pack in {}<game ID>. Prefetching reads the whole pack "
                        "while the game starts, which avoids stutter but takes memory.",
                        File::GetUserPath(D_HIRESTEXTURES_IDX)));

  page.Header("Image quality");
  page.Custom("Anti-aliasing", {"Off", "2× MSAA", "4× MSAA", "2× SSAA", "4× SSAA"},
              AntiAliasingBinding(page.GetContext()));
  page.Toggle("Scaled EFB copy", Config::GFX_HACK_COPY_EFB_SCALED);
  page.Toggle("Per-pixel lighting", Config::GFX_ENABLE_PIXEL_LIGHTING);
  page.Toggle("Force 24-bit colour", Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
  page.Toggle("Disable fog", Config::GFX_DISABLE_FOG);
  page.Toggle("Disable copy filter", Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
  page.Note("SSAA shades every sample rather than every pixel, which smooths textures, but "
            "costs far more GPU time than MSAA.");

  std::vector<Option<std::string>> shaders = {{"Off", ""}};
  for (const std::string& shader : VideoCommon::PostProcessing::GetShaderList())
    shaders.push_back({shader, shader});

  page.Header("Post-processing");
  page.Choice("Shader", Config::GFX_ENHANCE_POST_SHADER, std::move(shaders));
  page.Note(fmt::format("Put your own shaders in {}. The shader can also be changed from the "
                        "pause menu while playing.",
                        File::GetUserPath(D_SHADERS_IDX)));

  const ContextPtr context = page.GetContext();
  const DependentsPtr colour_space =
      ShowWhen([context] { return context->Read(Config::GFX_CC_CORRECT_COLOR_SPACE); });
  const DependentsPtr gamma =
      ShowWhen([context] { return context->Read(Config::GFX_CC_CORRECT_GAMMA); });

  page.Header("Colour correction");
  page.Toggle("Correct colour space", Config::GFX_CC_CORRECT_COLOR_SPACE, false,
              RefreshOf({colour_space}));
  colour_space->Add(page.Choice("Game colour space", Config::GFX_CC_GAME_COLOR_SPACE,
                                {{"NTSC-M (SMPTE 170M)", ColorCorrectionRegion::SMPTE_NTSCM},
                                 {"NTSC-J (ARIB TR-B9)", ColorCorrectionRegion::SYSTEMJ_NTSCJ},
                                 {"PAL (EBU)", ColorCorrectionRegion::EBU_PAL}}));
  page.Toggle("Correct gamma", Config::GFX_CC_CORRECT_GAMMA, false, RefreshOf({gamma}));
  gamma->Add(page.Choice(
      "Game gamma", Config::GFX_CC_GAME_GAMMA,
      {{"2.20", 2.2f}, {"2.35 (default)", 2.35f}, {"2.50", 2.5f}, {"2.60", 2.6f}, {"2.80", 2.8f}}));

  page.Header("Graphics mods");
  page.Toggle("Enable graphics mods", Config::GFX_MODS_ENABLE);
  page.Note(page.IsPerGame() ? std::string("Choose this game's mods from its Graphics mods tab.") :
                               fmt::format("Choose mods for each game from its Graphics mods tab. "
                                           "Put your own in {}.",
                                           File::GetUserPath(D_GRAPHICSMOD_IDX)));
}

void BuildHacks(PageBuilder& page)
{
  page.Header("Embedded frame buffer");
  page.Toggle("Skip EFB access from CPU", Config::GFX_HACK_EFB_ACCESS_ENABLE, true);
  page.Toggle("Ignore format changes", Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES, true);
  page.Toggle("Store EFB copies to texture only", Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
  page.Toggle("Defer EFB copies to RAM", Config::GFX_HACK_DEFER_EFB_COPIES);
  page.Toggle("Defer EFB cache invalidation", Config::GFX_HACK_EFB_DEFER_INVALIDATION);
  page.Toggle("Disable EFB VRAM copies", Config::GFX_HACK_DISABLE_COPY_TO_VRAM);
  page.Note("May result in a significant performance boost in some cases, but also may result in "
            "crashes.");

  page.Header("Texture cache");
  page.Choice("Accuracy", Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES,
              {{"Fast", 128}, {"Balanced", 512}, {"Safe", 0}});
  page.Toggle("GPU texture decoding", Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
  page.Toggle("Fast texture sampling", Config::GFX_HACK_FAST_TEXTURE_SAMPLING);
  page.Toggle("Save texture cache to state", Config::GFX_SAVE_TEXTURE_CACHE_TO_STATE);
  page.Note("Saving the texture cache makes save states larger, but keeps some games from "
            "drawing stale textures after loading one.");

  page.Header("External frame buffer");
  page.Toggle("Store XFB copies to texture only", Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
  page.Toggle("Immediately present XFB", Config::GFX_HACK_IMMEDIATE_XFB);
  page.Toggle("Skip presenting duplicate frames", Config::GFX_HACK_SKIP_DUPLICATE_XFBS);

  page.Header("Other");
  page.Toggle("Fast depth calculation", Config::GFX_FAST_DEPTH_CALC);
  page.Toggle("Disable bounding box", Config::GFX_HACK_BBOX_ENABLE, true);
  page.Toggle("Vertex rounding", Config::GFX_HACK_VERTEX_ROUNDING);
  page.Toggle("VBI skip", Config::GFX_HACK_VI_SKIP);
  page.Toggle("Cull vertices on the CPU", Config::GFX_CPU_CULL);
  page.Toggle("Expand points and lines in the vertex shader",
              Config::GFX_PREFER_VS_FOR_LINE_POINT_EXPANSION);
}

#ifdef HAS_FRAME_GENERATION
std::jthread s_frame_generation_thread;

std::string FrameGenerationShaderStatus()
{
  switch (Deko3D::FrameGeneration::GetShaderStatus())
  {
  case Deko3D::FrameGeneration::ShaderStatus::Prepared:
    return "Prepared";
  case Deko3D::FrameGeneration::ShaderStatus::NotPrepared:
    return "Not prepared";
  case Deko3D::FrameGeneration::ShaderStatus::MissingDll:
  default:
    return "No Lossless.dll";
  }
}

void PrepareFrameGenerationShaders(brls::DetailCell* cell)
{
  if (!File::Exists(Deko3D::FrameGeneration::GetDllPath()))
  {
    auto* dialog = new brls::Dialog(fmt::format(
        "Copy Lossless.dll from your own copy of Lossless Scaling to {}, then prepare the shaders "
        "again.",
        Deko3D::FrameGeneration::GetDllPath()));
    dialog->addButton("OK", [] {});
    dialog->open();
    return;
  }

  auto* label = new brls::Label();
  label->setText("Reading Lossless.dll...");
  label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
  label->setMargins(32, 32, 32, 32);

  auto* content = new brls::Box(brls::Axis::COLUMN);
  content->addView(label);

  auto* dialog = new brls::Dialog(content);
  dialog->open();

  s_frame_generation_thread = std::jthread([dialog, label, cell] {
    const std::string error =
        Deko3D::FrameGeneration::PrepareShaders([label](u32 compiled, u32 total) {
          brls::sync([label, compiled, total] {
            label->setText(fmt::format("Compiling shader {} of {}...", compiled, total));
          });
        });

    brls::sync([dialog, cell, error] {
      dialog->close();
      cell->setDetailText(FrameGenerationShaderStatus());
      brls::Application::notify(error.empty() ? "Frame generation shaders prepared." : error);
    });
  });
}

void BuildFrameGeneration(PageBuilder& page)
{
  page.Header("Frame generation");
  page.Toggle("Enable frame generation", Config::GFX_FRAME_GENERATION);
  page.Choice("Multiplier", Config::GFX_FRAME_GENERATION_MULTIPLIER,
              {{"2×", 2u}, {"3×", 3u}, {"4×", 4u}});
  page.Choice("Flow scale", Config::GFX_FRAME_GENERATION_FLOW_SCALE,
              {{"100%", 1u}, {"50%", 2u}, {"33%", 3u}, {"25%", 4u}});
  page.Toggle("Performance mode", Config::GFX_FRAME_GENERATION_PERFORMANCE);
  page.Note("Frame generation raises both CPU and GPU requirements when enabled. You will need "
            "to overclock the system to get good results. Nezumiiruka's stastics counters also do "
            "not account for latency or cost from lsfg running.");
  page.Note("A lower flow scale and performance mode are both faster, at some cost to quality. "
            "Increasing the render resolution bumps the GPU workload for both it and framegen. "
            "Currently, only the Deko3D renderer supports framegen.");
  page.Toggle("Allow above 60 Hz", Config::GFX_FRAME_GENERATION_HIGH_REFRESH_RATE);
  page.Note("Most people should leave this off. It generates frames however fast the game "
            "already runs, so a 60 FPS game at 2× presents 120 frames a second, or a 30 FPS "
            "game at 4× presents 120 FPS. Only enable it if your display has been overclocked "
            "to refresh faster than 60 Hz.");

  if (page.IsPerGame())
    return;

  page.Header("Shaders");
  auto cell = std::make_shared<brls::DetailCell*>(nullptr);
  *cell = page.Action("Prepare shaders", FrameGenerationShaderStatus(),
                      [cell] { PrepareFrameGenerationShaders(*cell); });
  page.Note(fmt::format("The shaders come from Lossless.dll, which you must supply from your own "
                        "copy of Lossless Scaling. Place Lossless.dll at {}. Preparing them takes "
                        "a few minutes and only has to be done again if the DLL changes.",
                        Deko3D::FrameGeneration::GetDllPath()));
}
#endif

void BuildEmulation(PageBuilder& page)
{
  page.Header("CPU");
  page.Choice("CPU engine", Config::MAIN_CPU_CORE,
              {{"JIT recompiler (recommended)", PowerPC::CPUCore::JITARM64},
               {"Cached interpreter (slow)", PowerPC::CPUCore::CachedInterpreter},
               {"Interpreter (very slow)", PowerPC::CPUCore::Interpreter}});
  page.Toggle("Dual core", Config::MAIN_CPU_THREAD);
  page.Note("Runs the emulated GPU on its own core. Turning it off is much slower, but can fix "
            "some crashes.");
  const std::vector<std::string> clock_labels = {"Off",  "50%",  "75%",  "90%",
                                                 "110%", "125%", "150%", "200%"};
  const std::vector<float> clock_factors = {0.5f, 0.75f, 0.9f, 1.1f, 1.25f, 1.5f, 2.0f};
  page.Custom("CPU clock override", clock_labels,
              OverclockBinding(page.GetContext(), Config::MAIN_OVERCLOCK_ENABLE,
                               Config::MAIN_OVERCLOCK, clock_factors));
  page.Note("Underclocking the emulated CPU can make demanding games reach full speed, at the "
            "risk of slowdown inside the game itself.");
  page.Custom("VBI frequency override", clock_labels,
              OverclockBinding(page.GetContext(), Config::MAIN_VI_OVERCLOCK_ENABLE,
                               Config::MAIN_VI_OVERCLOCK, clock_factors));
  page.Note(
      "Changes how often the console refreshes the screen, with the emulated CPU scaled to "
      "match. Games whose frame rate is tied to it run at a different frame rate, so lowering "
      "it makes them less demanding and raising it makes them smoother. Can cause crashes.");

  page.Header("Accuracy");
  page.Toggle("Enable MMU", Config::MAIN_MMU);
  page.Toggle("Accurate CPU cache", Config::MAIN_ACCURATE_CPU_CACHE);
  page.Toggle("Enable FPRF", Config::MAIN_FPRF);
  page.Toggle("Accurate NaNs", Config::MAIN_ACCURATE_NANS);
  page.Toggle("Synchronise GPU thread", Config::MAIN_SYNC_GPU);
  page.Toggle("Synchronise on idle skipping", Config::MAIN_SYNC_ON_SKIP_IDLE);
  page.Toggle("Emulate disc speed", Config::MAIN_FAST_DISC_SPEED, true);

  page.Header("Timing");
  page.Toggle("Correct time drift", Config::MAIN_CORRECT_TIME_DRIFT);
  page.Toggle("Rush frame presentation", Config::MAIN_RUSH_FRAME_PRESENTATION);
  page.Toggle("Smooth early presentation", Config::MAIN_SMOOTH_EARLY_PRESENTATION);

  page.Header("Region");
  if (!page.IsPerGame())
  {
    page.Choice("Fallback region", Config::MAIN_FALLBACK_REGION,
                {{"NTSC-J", DiscIO::Region::NTSC_J},
                 {"NTSC-U", DiscIO::Region::NTSC_U},
                 {"PAL", DiscIO::Region::PAL},
                 {"NTSC-K", DiscIO::Region::NTSC_K}});
  }
  page.Toggle("Allow mismatched region settings", Config::MAIN_OVERRIDE_REGION_SETTINGS);
  page.Note("Lets the system language and other settings stay as chosen even when they do not "
            "match the game's region.");

  const ContextPtr context = page.GetContext();
  const DependentsPtr custom_rtc =
      ShowWhen([context] { return context->Read(Config::MAIN_CUSTOM_RTC_ENABLE); });

  page.Header("Clock");
  page.Toggle("Use a custom date and time", Config::MAIN_CUSTOM_RTC_ENABLE, false,
              RefreshOf({custom_rtc}));
  custom_rtc->Add(AddRtcCell(page));
  page.Note("Otherwise the console clock follows the Switch's. The custom clock starts from the "
            "chosen time each boot.");
}

void BuildAudio(PageBuilder& page)
{
  page.Header("DSP");
  page.Choice("DSP emulation", Config::MAIN_DSP_HLE,
              {{"HLE (recommended)", true}, {"LLE interpreter (very slow)", false}});

  page.Header("Output");
  std::vector<Option<int>> volumes;
  for (int volume = 0; volume <= 100; volume += 10)
    volumes.push_back({volume == 0 ? std::string("Muted") : fmt::format("{}%", volume), volume});
  page.Choice("Volume", Config::MAIN_AUDIO_VOLUME, std::move(volumes));
  page.Choice("Buffer size", Config::MAIN_AUDIO_BUFFER_SIZE,
              {{"32 ms", 32},
               {"48 ms", 48},
               {"64 ms", 64},
               {"80 ms", 80},
               {"100 ms", 100},
               {"150 ms", 150},
               {"200 ms", 200}});
  page.Toggle("Fill audio gaps", Config::MAIN_AUDIO_FILL_GAPS);
  page.Toggle("Preserve pitch when speed changes", Config::MAIN_AUDIO_PRESERVE_PITCH);
  page.Toggle("Mute when the speed limit is off", Config::MAIN_AUDIO_MUTE_ON_DISABLED_SPEED_LIMIT);
}

void BuildGameCube(PageBuilder& page, const std::shared_ptr<const UICommon::GameFile>& game)
{
  page.Header("System");
  page.Choice(
      "System language", Config::MAIN_GC_LANGUAGE,
      {{"English", 0}, {"German", 1}, {"French", 2}, {"Spanish", 3}, {"Italian", 4}, {"Dutch", 5}});
  page.Toggle("Skip main menu", Config::MAIN_SKIP_IPL);

  using ExpansionInterface::EXIDeviceType;
  const std::vector<Option<EXIDeviceType>> slot_devices = {
      {"Nothing", EXIDeviceType::None},
      {"Memory card (GCI folder)", EXIDeviceType::MemoryCardFolder},
      {"Memory card (raw file)", EXIDeviceType::MemoryCard},
  };

  page.Header("Memory cards");
  page.Choice("Slot A", Config::MAIN_SLOT_A, slot_devices);
  page.Choice("Slot B", Config::MAIN_SLOT_B, slot_devices);
  if (game)
  {
    page.Action("Manage this game's saves", "",
                [game] { brls::Application::pushActivity(CreateMemoryCardsActivity(*game)); });
  }
  else
  {
    page.Action("Manage memory cards", "",
                [] { brls::Application::pushActivity(CreateMemoryCardsActivity()); });
  }

  const ContextPtr context = page.GetContext();
  const auto port_is = [context](std::initializer_list<EXIDeviceType> types) {
    return ShowWhen([context, types = std::vector<EXIDeviceType>(types)] {
      return std::ranges::find(types, context->Read(Config::MAIN_SERIAL_PORT_1)) != types.end();
    });
  };
  const DependentsPtr built_in = port_is({EXIDeviceType::EthernetBuiltIn});
  const DependentsPtr xlink = port_is({EXIDeviceType::EthernetXLink});
  const DependentsPtr tap_server = port_is({EXIDeviceType::EthernetTapServer});
  const DependentsPtr modem = port_is({EXIDeviceType::ModemTapServer});

  page.Header("Network adapter");
  page.Choice("Serial port 1", Config::MAIN_SERIAL_PORT_1,
              {{"Nothing", EXIDeviceType::None},
               {"Broadband Adapter (built in)", EXIDeviceType::EthernetBuiltIn},
               {"Broadband Adapter (XLink Kai)", EXIDeviceType::EthernetXLink},
               {"Broadband Adapter (tapserver)", EXIDeviceType::EthernetTapServer},
               {"Modem Adapter (tapserver)", EXIDeviceType::ModemTapServer}},
              RefreshOf({built_in, xlink, tap_server, modem}));
  built_in->Add(page.Text("DNS server", Config::MAIN_BBA_BUILTIN_DNS, "3.18.217.27"));
  built_in->Add(page.Note("The built in adapter connects games straight to the internet through "
                          "the Switch's connection."));
  xlink->Add(page.Text("XLink Kai address", Config::MAIN_BBA_XLINK_IP, "127.0.0.1"));
  xlink->Add(page.Note("The address of a computer on your network running XLink Kai."));
  tap_server->Add(
      page.Text("tapserver address", Config::MAIN_BBA_TAPSERVER_DESTINATION, "192.168.1.2:7777"));
  modem->Add(
      page.Text("tapserver address", Config::MAIN_MODEM_TAPSERVER_DESTINATION, "192.168.1.2:7778"));
  tap_server->Add(page.Note("The address and port of a computer on your network running "
                            "tapserver."));
  modem->Add(page.Note("The address and port of a computer on your network running tapserver."));
}

void BuildWii(PageBuilder& page)
{
  page.Header("System");
  page.Choice("System language", Config::SYSCONF_LANGUAGE,
              {{"Japanese", 0u},
               {"English", 1u},
               {"German", 2u},
               {"French", 3u},
               {"Spanish", 4u},
               {"Italian", 5u},
               {"Dutch", 6u},
               {"Simplified Chinese", 7u},
               {"Traditional Chinese", 8u},
               {"Korean", 9u}});
  page.Choice("Aspect ratio", Config::SYSCONF_WIDESCREEN, {{"4:3", false}, {"16:9", true}});
  page.Choice("Sound", Config::SYSCONF_SOUND_MODE,
              {{"Mono", 0u}, {"Stereo", 1u}, {"Surround", 2u}});
  page.Toggle("PAL60 mode", Config::SYSCONF_PAL60);
  page.Toggle("Progressive scan", Config::SYSCONF_PROGRESSIVE_SCAN);
  page.Toggle("Screen burn-in reduction", Config::SYSCONF_SCREENSAVER);

  page.Header("Wii Remote");
  page.Choice("Sensor bar position", Config::SYSCONF_SENSOR_BAR_POSITION,
              {{"Below the screen", 0u}, {"Above the screen", 1u}});
  page.Choice("IR sensitivity", Config::SYSCONF_SENSOR_BAR_SENSITIVITY,
              {{"1", 1u}, {"2", 2u}, {"3", 3u}, {"4", 4u}, {"5", 5u}});
  page.Choice("Speaker volume", Config::SYSCONF_SPEAKER_VOLUME,
              {{"Muted", 0u}, {"Low", 32u}, {"Medium", 64u}, {"Default", 88u}, {"Maximum", 127u}});
  page.Toggle("Rumble", Config::SYSCONF_WIIMOTE_MOTOR);

  page.Header("SD card");
  page.Toggle("Insert SD card", Config::MAIN_WII_SD_CARD);
  page.Toggle("Allow writes to the SD card", Config::MAIN_ALLOW_SD_WRITES);
  page.Toggle("Sync with a folder", Config::MAIN_WII_SD_CARD_ENABLE_FOLDER_SYNC);
  page.Note(fmt::format("Packs {} into the SD card image when a game starts, and unpacks it back "
                        "when the game ends.",
                        File::GetUserPath(D_WIISDCARDSYNCFOLDER_IDX)));
  if (!page.IsPerGame())
  {
    page.Choice("Size of a new SD card", Config::MAIN_WII_SD_CARD_FILESIZE,
                {{"Auto", 0ull},
                 {"64 MiB", 64ull << 20},
                 {"128 MiB", 128ull << 20},
                 {"256 MiB", 256ull << 20},
                 {"512 MiB", 512ull << 20},
                 {"1 GiB", 1ull << 30},
                 {"2 GiB", 2ull << 30}});
    page.Action("Pack the folder into the SD card", "", PackSDCard);
    page.Action("Unpack the SD card into the folder", "", UnpackSDCard);
    page.Info("SD card image", File::GetUserPath(F_WIISDCARDIMAGE_IDX));
  }

  page.Header("Peripherals");
  page.Toggle("Connect USB keyboard", Config::MAIN_WII_KEYBOARD);
  page.Toggle("Emulate Skylanders portal", Config::MAIN_EMULATE_SKYLANDER_PORTAL);
  page.Toggle("Emulate Disney Infinity base", Config::MAIN_EMULATE_INFINITY_BASE);
  page.Note(fmt::format("Place and remove figures from the pause menu while playing. Figure files "
                        "live in {} and {}.",
                        FiguresSwitch::GetSkylanderDirectory(),
                        FiguresSwitch::GetInfinityDirectory()));

  if (!page.IsPerGame())
  {
    page.Header("Online");
    page.Toggle("Enable WiiConnect24 via WiiLink", Config::MAIN_WII_WIILINK_ENABLE);
    page.Note("See the setup guide at https://wiilink.ca/");
    page.Note("Read the Terms of Service at https://www.wiilink24.com/tos");
  }
}

ProfileBinding GlobalProfileBinding(ControllerProfiles::Kind kind, int slot)
{
  ProfileBinding binding;
  binding.current = [kind, slot] { return ControllerProfiles::GetGlobalProfile(kind, slot); };
  binding.write = [kind, slot](const std::string& name) {
    ControllerProfiles::SetGlobalProfile(kind, slot, name);
  };
  return binding;
}

ProfileBinding GameProfileBinding(const ContextPtr& context, ControllerProfiles::Kind kind,
                                  int slot)
{
  const Config::Info<std::string>& info = ControllerProfiles::GetGameProfileInfo(kind, slot);

  ProfileBinding binding;
  binding.current = [context, &info] { return context->Read(info); };
  binding.write = [context, &info](const std::string& name) { context->Write(info, name); };
  binding.inherited = [context, &info, kind, slot] {
    const std::string name = context->ReadInherited(info);
    return name.empty() ? ControllerProfiles::GetGlobalProfile(kind, slot) : name;
  };
  binding.overridden = [context, &info] { return context->IsOverridden(info.GetLocation()); };
  binding.clear = [context, &info] { context->Clear(info.GetLocation()); };
  return binding;
}

void AddProfileCell(PageBuilder& page, const std::string& title, ControllerProfiles::Kind kind,
                    int slot)
{
  page.Add(CreateProfileCell(title, kind,
                             page.IsPerGame() ? GameProfileBinding(page.GetContext(), kind, slot) :
                                                GlobalProfileBinding(kind, slot)));
}

void ShowControllerApplet()
{
  hidSetNpadJoyHoldType(HidNpadJoyHoldType_Horizontal);

  HidLaControllerSupportArg arg;
  hidLaCreateControllerSupportArg(&arg);

  HidLaControllerSupportResultInfo info{};
  if (R_FAILED(hidLaShowControllerSupport(&info, &arg)))
  {
    brls::Application::notify("Controllers can only be paired when not in applet mode.");
    return;
  }

  brls::Application::notify(info.player_count == 1 ?
                                std::string("1 player connected.") :
                                fmt::format("{} players connected.", info.player_count));
}

#ifdef HAS_LIBMGBA
std::string DescribeFile(const std::string& path, const std::string& empty)
{
  if (path.empty())
    return empty;

  std::string name, extension;
  SplitPath(path, nullptr, &name, &extension);
  return name + extension;
}

void AddFileCell(PageBuilder& page, const std::string& title, const Config::Info<std::string>& info,
                 const std::string& empty, const std::string& picker_title,
                 std::vector<std::string> extensions,
                 std::function<void(const std::string&)> on_changed = {})
{
  const ContextPtr context = page.GetContext();

  auto* cell = new brls::DetailCell();
  cell->setText(title);
  cell->setDetailText(DescribeFile(context->Read(info), empty));

  const auto set = [context, &info, cell, empty, on_changed](const std::string& path) {
    context->Write(info, path);
    cell->setDetailText(DescribeFile(path, empty));
    if (on_changed)
      on_changed(path);
  };

  cell->registerClickAction([context, &info, title, picker_title, extensions, set](brls::View*) {
    const std::string current = context->Read(info);
    const auto pick = [picker_title, extensions, set, current] {
      const std::string directory = current.empty() ?
                                        std::string("sdmc:/") :
                                        current.substr(0, current.find_last_of('/') + 1);
      brls::Application::pushActivity(
          CreateFilePickerActivity(picker_title, directory, extensions, set));
    };

    if (current.empty())
    {
      pick();
      return true;
    }

    auto* dialog = new brls::Dialog(fmt::format("{}: {}", title, DescribeFile(current, {})));
    dialog->addButton("Cancel", [] {});
    dialog->addButton("Remove", [set] { set({}); });
    dialog->addButton("Change", pick);
    dialog->open();
    return true;
  });

  page.Add(cell);
}

void BuildGBA(PageBuilder& page)
{
  page.Header("Game Boy Advance");
  AddFileCell(page, "BIOS", Config::MAIN_GBA_BIOS_PATH, "Default location",
              "Choose a GBA BIOS (gba_bios.bin)", {".bin"}, [](const std::string& path) {
                File::SetUserPath(F_GBABIOS_IDX, path.empty() ?
                                                     File::GetUserPath(D_GBAUSER_IDX) + GBA_BIOS :
                                                     path);
              });
  page.Note(
      fmt::format("The default location is {}{}.", File::GetUserPath(D_GBAUSER_IDX), GBA_BIOS));

  for (int port = 0; port < ControllerProfiles::SLOT_COUNT; ++port)
  {
    AddFileCell(page, fmt::format("GBA {} cartridge", port + 1), Config::MAIN_GBA_ROM_PATHS[port],
                "None", fmt::format("Choose a cartridge for GBA {}", port + 1),
                {".gba", ".agb", ".mb", ".gb", ".gbc", ".bin", ".zip", ".7z"});
  }
  page.Note("Most link games don't require any ROM.");

  page.Toggle("Keep saves next to the cartridge", Config::MAIN_GBA_SAVES_IN_ROM_PATH);
  page.Note("A GBA in port N is controlled by player N's controller.");
}
#endif

void BuildControls(PageBuilder& page, bool wii)
{
  using ControllerProfiles::Kind;

  if (!page.IsPerGame())
  {
    page.Header("Controllers");
    page.Action("Change grip and order", "", ShowControllerApplet);
  }

  std::vector<Option<SerialInterface::SIDevices>> port_devices = {
      {"Nothing", SerialInterface::SIDEVICE_NONE},
      {"Standard controller", SerialInterface::SIDEVICE_GC_CONTROLLER},
      {"GameCube adapter", SerialInterface::SIDEVICE_WIIU_ADAPTER},
      {"Steering wheel", SerialInterface::SIDEVICE_GC_STEERING},
      {"DK Bongos", SerialInterface::SIDEVICE_GC_TARUKONGA},
  };
#ifdef HAS_LIBMGBA
  port_devices.push_back({"Game Boy Advance", SerialInterface::SIDEVICE_GC_GBA_EMULATED});
#endif

  page.Header("GameCube controller ports");
  for (int port = 0; port < ControllerProfiles::SLOT_COUNT; ++port)
  {
    if (!page.IsPerGame())
    {
      const ContextPtr context = page.GetContext();
      const DependentsPtr adapter = ShowWhen([context, port] {
        return context->Read(Config::GetInfoForSIDevice(port)) ==
               SerialInterface::SIDEVICE_WIIU_ADAPTER;
      });
      page.Choice(fmt::format("Port {}", port + 1), Config::GetInfoForSIDevice(port), port_devices,
                  RefreshOf({adapter}));
      adapter->Add(page.Toggle(fmt::format("Port {}: treat as DK Bongos", port + 1),
                               Config::GetInfoForSimulateKonga(port)));
    }
    AddProfileCell(page, fmt::format("Port {} profile", port + 1), Kind::GCPad, port);
  }

  if (!page.IsPerGame())
  {
    page.Note("The GameCube adapter passes a controller on the official adapter straight through, "
              "without a profile. Port N takes player N's GameCube controller.");
  }

  if (wii)
  {
    page.Header("Wii Remotes");
    for (int index = 0; index < ControllerProfiles::SLOT_COUNT; ++index)
    {
      page.Choice(fmt::format("Wii Remote {}", index + 1), Config::GetInfoForWiimoteSource(index),
                  {{"Nothing", WiimoteSource::None}, {"Emulated", WiimoteSource::Emulated}});
      AddProfileCell(page, fmt::format("Wii Remote {} profile", index + 1), Kind::Wiimote, index);
    }
    page.Note("Wii Remote 1 is player 1's controller, and so on. By default, HOME is right stick "
              "click and recentre pointer is on left stick click.");
  }

  if (page.IsPerGame())
    return;

#ifdef HAS_LIBMGBA
  BuildGBA(page);
#endif

  page.Header("Profiles");
  for (const Kind kind : {Kind::GCPad, Kind::Wiimote})
  {
    page.Action(fmt::format("{} profiles", ControllerProfiles::GetKindLabel(kind)), "",
                [kind] { brls::Application::pushActivity(CreateProfileManagerActivity(kind)); });
  }
}

#ifdef USE_RETRO_ACHIEVEMENTS
void BuildAchievements(PageBuilder& page)
{
  page.Header("RetroAchievements");
  page.Add(CreateAchievementAccountView());
  page.Note("Create an account at retroachievements.org first.");

  page.Header("Hardcore mode");
  page.Toggle("Enable hardcore mode", Config::RA_HARDCORE_ENABLED);
  page.Note("Cheats, save states, and speeds below 100% are unavailable while it is on.");

  page.Header("Options");
  page.Toggle("Unofficial achievements", Config::RA_UNOFFICIAL_ENABLED);
  page.Toggle("Encore mode", Config::RA_ENCORE_ENABLED);
  page.Note("Encore mode lets achievements you have already unlocked trigger again.");
  page.Toggle("Spectator mode", Config::RA_SPECTATOR_ENABLED);
  page.Note("Spectator mode tracks achievements without submitting anything to the site.");

  page.Header("On-screen display");
  page.Toggle("Leaderboard trackers", Config::RA_LEADERBOARD_TRACKER_ENABLED);
  page.Toggle("Challenge indicators", Config::RA_CHALLENGE_INDICATORS_ENABLED);
  page.Toggle("Progress notifications", Config::RA_PROGRESS_ENABLED);
  page.Note("Unlock notifications need on-screen messages, toggleable under General.");
}

void BuildGameAchievements(PageBuilder& page, const UICommon::GameFile& game)
{
  page.Header("RetroAchievements");
  if (!Config::Get(Config::RA_ENABLED) || Config::Get(Config::RA_API_TOKEN).empty())
  {
    page.Note("Enable RetroAchievements and log in under Settings to see this game's "
              "achievements.");
    return;
  }

  page.Add(CreateGameAchievementsView(game.GetFilePath()));
}
#endif

void BuildGameCheats(PageBuilder& page, const std::shared_ptr<const UICommon::GameFile>& game)
{
  page.Header("Cheats");
  page.Toggle("Enable cheats", Config::MAIN_ENABLE_CHEATS);
  page.Note("Codes switched on below only run while cheats are enabled.");
#ifdef USE_RETRO_ACHIEVEMENTS
  if (Config::Get(Config::RA_ENABLED) && Config::Get(Config::RA_HARDCORE_ENABLED))
    page.Note("Hardcore mode is on. Only codes approved by RetroAchievements will run.");
#endif
  page.Add(CreateGameCheatsView(game));
}

std::string DescribeSystemMenu()
{
  const std::string description = GetSystemMenuDescription();
  return description.empty() ? std::string("Not installed") : description;
}

void BuildWiiSystem(PageBuilder& page, const std::function<void()>& launch_system_menu)
{
  page.Header("Wii Menu");
  brls::DetailCell* menu = page.Action("Start the Wii Menu", DescribeSystemMenu(),
                                       [launch_system_menu] { launch_system_menu(); });
  const NANDChangedFn refresh = [menu] { menu->setDetailText(DescribeSystemMenu()); };
  page.Action("Update online", "", [refresh] { PerformOnlineUpdate(refresh); });
  page.Note("Downloads and installs the latest Wii system software.");

  page.Header("Titles");
  page.Action("Install a WAD", "", [refresh] { ChooseAndInstallWAD(refresh); });
  page.Note("WADs in the game folder can also be installed or uninstalled via their options.");

  page.Header("System memory");
  page.Action("Import a BootMii NAND backup", "", [refresh] { ImportNANDBackup(refresh); });
  page.Action("Check for problems", "", [refresh] { CheckNAND(refresh); });
  page.Action("Extract certificates", "", ExtractCertificates);
  page.Info("Location", File::GetUserPath(D_WIIROOT_IDX));

  page.Header("Saves");
  page.Action("Import a save", "", ImportWiiSave);
  page.Action("Export every save", "", ExportWiiSaves);
  page.Note(fmt::format("Saves are exported to {}WiiSaves.", File::GetUserPath(D_USER_IDX)));
}

void AddSystemMemoryActions(PageBuilder& page, const UICommon::GameFile& game)
{
  if (game.GetPlatform() == DiscIO::Platform::WiiDisc)
  {
    page.Action("Perform a system update", "",
                [path = game.GetFilePath()] { PerformDiscUpdate(path); });
    return;
  }

  if (game.GetPlatform() != DiscIO::Platform::WiiWAD)
    return;

  const u64 title_id = game.GetTitleID();
  const auto describe = [title_id] {
    return IsTitleInstalled(title_id) ? std::string("Installed") : std::string("Not installed");
  };

  auto install = std::make_shared<brls::DetailCell*>(nullptr);
  const NANDChangedFn refresh = [install, describe] { (*install)->setDetailText(describe()); };
  *install = page.Action("Install to the Wii system memory", describe(),
                         [path = game.GetFilePath(), refresh] { InstallWAD(path, refresh); });

  page.Action("Uninstall from the Wii system memory", "", [title_id, refresh] {
    if (IsTitleInstalled(title_id))
      UninstallTitle(title_id, refresh);
    else
      brls::Application::notify("This title is not installed.");
  });
}

void BuildDebugging(PageBuilder& page)
{
  using Common::Log::LogLevel;
  using Common::Log::LogListener;
  using Common::Log::LogManager;

  page.Header("Logging");
  std::vector<Option<LogLevel>> levels = {{"Notice", LogLevel::LNOTICE},
                                          {"Error", LogLevel::LERROR},
                                          {"Warning", LogLevel::LWARNING},
                                          {"Info", LogLevel::LINFO}};
  if constexpr (Common::Log::MAX_EFFECTIVE_LOGLEVEL >= LogLevel::LDEBUG)
    levels.push_back({"Debug", LogLevel::LDEBUG});
  page.Choice("Verbosity", Common::Log::LOGGER_VERBOSITY, std::move(levels));

  auto* write_to_file = new brls::BooleanCell();
  write_to_file->init("Write the log to a file",
                      LogManager::GetInstance()->IsListenerEnabled(LogListener::FILE_LISTENER),
                      [](bool on) {
                        LogManager* manager = LogManager::GetInstance();
                        manager->EnableListener(LogListener::FILE_LISTENER, on);
                        manager->SaveSettings();
                      });
  page.Add(write_to_file);
  page.Info("Log file", File::GetUserPath(F_MAINLOG_IDX));

  page.Header("Overlays");
  page.Toggle("Show rendering statistics", Config::GFX_OVERLAY_STATS);
  page.Toggle("Show projection statistics", Config::GFX_OVERLAY_PROJ_STATS);
  page.Toggle("Show texture formats", Config::GFX_TEXFMT_OVERLAY_ENABLE);

  const ContextPtr context = page.GetContext();
  const DependentsPtr texture_dumping =
      ShowWhen([context] { return context->Read(Config::GFX_DUMP_TEXTURES); });

  page.Header("Dumping");
  page.Toggle("Dump textures", Config::GFX_DUMP_TEXTURES, false, RefreshOf({texture_dumping}));
  texture_dumping->Add(page.Toggle("Dump base textures", Config::GFX_DUMP_BASE_TEXTURES));
  texture_dumping->Add(page.Toggle("Dump mipmaps", Config::GFX_DUMP_MIP_TEXTURES));
  page.Toggle("Dump EFB targets", Config::GFX_DUMP_EFB_TARGET);
  page.Toggle("Dump XFB targets", Config::GFX_DUMP_XFB_TARGET);
  page.Note(fmt::format("Dumps go to {}. Dumping is slow and really should be done on PC.",
                        File::GetUserPath(D_DUMP_IDX)));

  page.Header("CPU");
  page.Toggle("Fastmem", Config::MAIN_FASTMEM);
  page.Note("Turning fastmem off is much slower, but may fix a crash.");

  page.Toggle("Interpret everything", Config::MAIN_DEBUG_JIT_OFF);
  page.Toggle("Interpret loads and stores", Config::MAIN_DEBUG_JIT_LOAD_STORE_OFF);
  page.Toggle("Interpret lbzx", Config::MAIN_DEBUG_JIT_LOAD_STORE_LBZX_OFF);
  page.Toggle("Interpret lXz", Config::MAIN_DEBUG_JIT_LOAD_STORE_LXZ_OFF);
  page.Toggle("Interpret lwz", Config::MAIN_DEBUG_JIT_LOAD_STORE_LWZ_OFF);
  page.Toggle("Interpret floating-point loads and stores",
              Config::MAIN_DEBUG_JIT_LOAD_STORE_FLOATING_OFF);
  page.Toggle("Interpret paired loads and stores", Config::MAIN_DEBUG_JIT_LOAD_STORE_PAIRED_OFF);
  page.Toggle("Interpret floating point", Config::MAIN_DEBUG_JIT_FLOATING_POINT_OFF);
  page.Toggle("Interpret integer", Config::MAIN_DEBUG_JIT_INTEGER_OFF);
  page.Toggle("Interpret paired singles", Config::MAIN_DEBUG_JIT_PAIRED_OFF);
  page.Toggle("Interpret system registers", Config::MAIN_DEBUG_JIT_SYSTEM_REGISTERS_OFF);
  page.Toggle("Interpret branches", Config::MAIN_DEBUG_JIT_BRANCH_OFF);
  page.Toggle("Disable the register cache", Config::MAIN_DEBUG_JIT_REGISTER_CACHE_OFF);
  page.Note("Don't mess with these if you don't know what you are doing.");
}

void BuildGameGraphicsMods(PageBuilder& page, const UICommon::GameFile& game)
{
  page.Header("Graphics mods");
  if (!Config::Get(Config::GFX_MODS_ENABLE))
  {
    page.Note("Graphics mods are switched off. Turn them on under Enhancements.");
  }

  auto group = std::make_shared<GraphicsModGroupConfig>(game.GetGameID());
  group->Load();

  if (group->GetMods().empty())
  {
    page.Note(fmt::format("No mods are available for this game. Put mods in {}.",
                          File::GetUserPath(D_GRAPHICSMOD_IDX)));
    return;
  }

  for (GraphicsModConfig& mod : group->GetMods())
  {
    auto* cell = new brls::BooleanCell();
    cell->init(mod.m_title, mod.m_enabled, [group, &mod](bool on) {
      mod.m_enabled = on;
      group->SetChangeCount(group->GetChangeCount() + 1);
      group->Save();
    });
    page.Add(cell);

    if (!mod.m_description.empty() || !mod.m_author.empty())
    {
      page.Note(mod.m_author.empty() ?
                    mod.m_description :
                    fmt::format("{}{}By {}.", mod.m_description,
                                mod.m_description.empty() ? "" : " ", mod.m_author));
    }
  }
  page.Note("Changes take effect the next time the game starts.");
}

void BuildAbout(PageBuilder& page)
{
  page.Header("Nezumiiruka");
  page.Info("Version", UpdaterSwitch::GetCurrentVersion());
  page.Info("Revision", Common::GetScmDescStr());
  page.Info("Branch", Common::GetScmBranchStr());
  page.Info("Build ID", Common::HorizonBuildId::GetHex().data());

  page.Header("Updates");
  page.Action("Check for updates", "", [] { CheckForUpdates(); });
  page.Action("Release notes", UpdaterSwitch::GetCurrentVersion(), [] { ShowReleaseNotes(); });
  page.Toggle("Check for updates on startup", Config::SWITCH_CHECK_FOR_UPDATES);
  page.Choice(
      "Update channel", Config::SWITCH_UPDATE_CHANNEL,
      {{"Stable", Config::UpdateChannel::Stable}, {"Beta", Config::UpdateChannel::Prerelease}});

  page.Header("System");
  SetSysFirmwareVersion firmware;
  if (R_SUCCEEDED(setsysGetFirmwareVersion(&firmware)))
    page.Info("Firmware", firmware.display_version);

  u64 total_memory = 0;
  u64 used_memory = 0;
  svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
  page.Info("Memory",
            fmt::format("{} of {} MiB in use", used_memory / 0x100000, total_memory / 0x100000));

  page.Header("Folders");
  page.Info("User folder", File::GetUserPath(D_USER_IDX));
  page.Info("Game folder", SwitchSettings::GetGameDirectory());
  for (const UsbStorage::Volume& volume : UsbStorage::GetVolumes())
    page.Info("USB drive", fmt::format("{} ({})", volume.root, volume.label));
  page.Info("Custom covers", File::GetUserPath(D_COVERCACHE_IDX));
  page.Info("Custom textures", File::GetUserPath(D_HIRESTEXTURES_IDX));
  page.Info("Riivolution", File::GetUserPath(D_RIIVOLUTION_IDX));

  page.Header("Licence");
  page.Note(
      "Nezumiiruka is a port of the Dolphin emulator, licensed under the GNU GPL version 2 or "
      "later. The menus use borealis, licensed under the Apache License 2.0. USB loading support "
      "is provided by libusbhsfs, licensed under the GNU GPL version 2 or later. Frame generation "
      "support is provided by LSFG-NX, which is licensed under the GNU GPL version 3 or later.");
}

void BuildLibrary(PageBuilder& page, const std::function<void()>& clear_cache)
{
  page.Header("Games");
  page.Text("Game folder", Config::SWITCH_GAME_DIRECTORY,
            File::GetUserPath(D_USER_IDX) + "roms" DIR_SEP);
  page.Note("Leave empty for the default folder. Subfolders are searched too.");
  page.Text("USB game folder", Config::SWITCH_USB_GAME_DIRECTORY, "Whole drive");
  page.Note("The folder(s) searched on every USB drive. Leave empty to search "
            "the whole drive.");
  page.Choice("Sort by", Config::SWITCH_GAME_LIST_SORT,
              {{"Title", Config::GameListSort::Title},
               {"Time played", Config::GameListSort::TimePlayed},
               {"File name", Config::GameListSort::FileName}});
  page.Toggle("Use the built-in list of game names", Config::MAIN_USE_BUILT_IN_TITLE_DATABASE);
  page.Note("Shows each game's full name from Dolphin's database, rather than the shortened "
            "name stored on the disc.");

  page.Header("Covers");
  page.Toggle("Download covers from GameTDB", Config::MAIN_USE_GAME_COVERS);
  page.Note("Covers are fetched while the game list is open. To use your own, put a PNG named "
            "<file name>.cover.png next to the game.");

  page.Header("Maintenance");
  page.Action("Rebuild the game list", "Clears cached titles and covers", [clear_cache] {
    auto* dialog = new brls::Dialog(
        "Forget every cached game and cover? They will be read and downloaded again.");
    dialog->addButton("Cancel", [] {});
    dialog->addButton("Rebuild", [clear_cache] {
      clear_cache();
      brls::Application::notify("The game list will be rebuilt.");
    });
    dialog->open();
  });
}

using RiivolutionDiscs = std::shared_ptr<std::vector<DiscIO::Riivolution::Disc>>;

size_t CountEnabledOptions(const std::vector<DiscIO::Riivolution::Disc>& discs)
{
  size_t count = 0;
  for (const DiscIO::Riivolution::Disc& disc : discs)
  {
    for (const DiscIO::Riivolution::Section& section : disc.m_sections)
    {
      count += std::ranges::count_if(section.m_options, [](const DiscIO::Riivolution::Option& o) {
        return o.m_selected_choice != 0;
      });
    }
  }
  return count;
}

std::string DescribeEnabledOptions(const std::vector<DiscIO::Riivolution::Disc>& discs)
{
  const size_t count = CountEnabledOptions(discs);
  if (count == 0)
    return "Nothing enabled";
  return count == 1 ? std::string("1 option enabled") : fmt::format("{} options enabled", count);
}

std::string GetXmlFileName(const DiscIO::Riivolution::Disc& disc)
{
  std::string filename, extension;
  SplitPath(disc.m_xml_path, nullptr, &filename, &extension);
  return filename + extension;
}

void BuildGameRiivolution(PageBuilder& page, const UICommon::GameFile& game,
                          const RiivolutionDiscs& discs, const std::function<void(bool)>& launch)
{
  page.Header("Riivolution");
  if (discs->empty())
  {
    page.Note(fmt::format(
        "No patches found for this game. Copy a mod's files into {} the way they would go on the "
        "root of an SD card so that its XML ends up in {}.",
        File::GetUserPath(D_RIIVOLUTION_IDX), RiivolutionSwitch::GetPatchDirectory()));
    return;
  }

  page.Action("Play with Riivolution patches", DescribeEnabledOptions(*discs),
              [launch] { launch(true); });

  const bool name_discs = discs->size() > 1;
  for (DiscIO::Riivolution::Disc& disc : *discs)
  {
    for (DiscIO::Riivolution::Section& section : disc.m_sections)
    {
      page.Header(section.m_name, name_discs ? GetXmlFileName(disc) : std::string());

      for (DiscIO::Riivolution::Option& option : section.m_options)
      {
        std::vector<std::string> labels{"Disabled"};
        for (const DiscIO::Riivolution::Choice& choice : option.m_choices)
          labels.push_back(choice.m_name);

        const int selected = option.m_selected_choice < labels.size() ?
                                 static_cast<int>(option.m_selected_choice) :
                                 0;

        auto* cell = new brls::SelectorCell();
        cell->init(option.m_name, labels, selected,
                   [discs, &option, game_id = game.GetGameID()](int index) {
                     option.m_selected_choice = static_cast<u32>(index);
                     RiivolutionSwitch::SaveChoices(game_id, *discs);
                   });
        page.Add(cell);
      }
    }
  }
}

void BuildGameInfo(PageBuilder& page, const UICommon::GameFile& game,
                   std::chrono::milliseconds time_played, const RiivolutionDiscs& riivolution,
                   const std::function<void(bool)>& launch)
{
  page.Header(GetTitle(game));

  const std::string& description =
      game.GetDescription(UICommon::GameFile::Variant::LongAndPossiblyCustom);
  if (!description.empty())
    page.Note(description);

  page.Action("Play", "", [launch] { launch(false); });
  if (riivolution && !riivolution->empty())
  {
    page.Action("Play with Riivolution patches", DescribeEnabledOptions(*riivolution),
                [launch] { launch(true); });
  }
  AddSystemMemoryActions(page, game);

  page.Header("Game");
  page.Info("Platform", GetPlatformName(game));
  page.Info("Region", GetRegionName(game));
  page.Info("Country", DiscIO::GetName(game.GetCountry(), false));
  page.Info("Maker", game.GetMaker(UICommon::GameFile::Variant::LongAndPossiblyCustom));
  page.Info("Game ID", game.GetGameID());
  page.Info("Title ID", GetTitleIdText(game));
  page.Info("Revision", GetGameIdDetail(game));
  page.Info("Internal name", game.GetInternalName());
  page.Info("Apploader date", game.GetApploaderDate());
  page.Info("Time played", FormatTimePlayed(time_played));

  page.Header("File");
  page.Info("File name", game.GetFileName());
  page.Info("Folder",
            game.GetFilePath().substr(0, game.GetFilePath().size() - game.GetFileName().size()));
  page.Info("Size", UICommon::FormatSize(game.GetFileSize()));
  page.Info("Format", GetFormatText(game));
}
}  // namespace

brls::Activity* CreateSettingsActivity(std::function<void()> on_closed,
                                       std::function<void()> clear_cache,
                                       std::function<void()> launch_system_menu)
{
  auto context = std::make_shared<SettingsContext>(std::move(on_closed));

  auto* tabs = new brls::TabFrame();
  AddPage(tabs, context, "General", BuildGeneral);
  AddPage(tabs, context, "Graphics", BuildGraphics);
  AddPage(tabs, context, "Enhancements", BuildEnhancements);
  AddPage(tabs, context, "Graphics hacks", BuildHacks);
#ifdef HAS_FRAME_GENERATION
  AddPage(tabs, context, "Frame generation", BuildFrameGeneration);
#endif
  tabs->addSeparator();
  AddPage(tabs, context, "Emulation", BuildEmulation);
  AddPage(tabs, context, "Audio", BuildAudio);
  AddPage(tabs, context, "GameCube", [](PageBuilder& page) { BuildGameCube(page, nullptr); });
  AddPage(tabs, context, "Wii", BuildWii);
  AddPage(tabs, context, "Controls", [](PageBuilder& page) { BuildControls(page, true); });
#ifdef USE_RETRO_ACHIEVEMENTS
  AddPage(tabs, context, "Achievements", BuildAchievements);
#endif
  tabs->addSeparator();
  tabs->addTab("Wii system", [context, launch_system_menu = std::move(launch_system_menu)] {
    return CreatePage(context, [&launch_system_menu](PageBuilder& page) {
      BuildWiiSystem(page, launch_system_menu);
    });
  });
  tabs->addTab("Game list", [context, clear_cache = std::move(clear_cache)] {
    return CreatePage(context,
                      [&clear_cache](PageBuilder& page) { BuildLibrary(page, clear_cache); });
  });
  AddPage(tabs, context, "Debugging", BuildDebugging);
  AddPage(tabs, context, "About", BuildAbout);

  auto* frame = new brls::AppletFrame(tabs);
  frame->setTitle("Settings");
  return new brls::Activity(frame);
}

brls::Activity* CreateGamePropertiesActivity(std::shared_ptr<const UICommon::GameFile> game,
                                             std::chrono::milliseconds time_played,
                                             std::function<void(bool riivolution)> launch)
{
  auto context = std::make_shared<SettingsContext>(*game);

  RiivolutionDiscs riivolution;
  if (DiscIO::IsDisc(game->GetPlatform()) && !game->IsModDescriptor())
  {
    riivolution = std::make_shared<std::vector<DiscIO::Riivolution::Disc>>(
        RiivolutionSwitch::LoadDiscs(*game));
  }

  auto* tabs = new brls::TabFrame();
  tabs->addTab("Information", [context, game, time_played, riivolution, launch] {
    return CreatePage(
        context,
        [&](PageBuilder& page) { BuildGameInfo(page, *game, time_played, riivolution, launch); },
        false);
  });
  if (riivolution)
  {
    tabs->addTab("Riivolution", [context, game, riivolution, launch] {
      return CreatePage(
          context,
          [&](PageBuilder& page) { BuildGameRiivolution(page, *game, riivolution, launch); },
          false);
    });
  }
  if (!game->GetGameID().empty())
  {
    tabs->addTab("Cheats", [context, game] {
      return CreatePage(context, [&](PageBuilder& page) { BuildGameCheats(page, game); }, false);
    });
  }
  if (!game->GetGameID().empty())
  {
    tabs->addTab("Graphics mods", [context, game] {
      return CreatePage(
          context, [&](PageBuilder& page) { BuildGameGraphicsMods(page, *game); }, false);
    });
  }
#ifdef USE_RETRO_ACHIEVEMENTS
  tabs->addTab("Achievements", [context, game] {
    return CreatePage(
        context, [&](PageBuilder& page) { BuildGameAchievements(page, *game); }, false);
  });
#endif
  tabs->addSeparator();
  AddPage(tabs, context, "General", BuildGeneral);
  AddPage(tabs, context, "Graphics", BuildGraphics);
  AddPage(tabs, context, "Enhancements", BuildEnhancements);
  AddPage(tabs, context, "Graphics hacks", BuildHacks);
#ifdef HAS_FRAME_GENERATION
  AddPage(tabs, context, "Frame generation", BuildFrameGeneration);
#endif
  AddPage(tabs, context, "Emulation", BuildEmulation);
  AddPage(tabs, context, "Audio", BuildAudio);
  if (DiscIO::IsWii(game->GetPlatform()))
  {
    AddPage(tabs, context, "Wii", BuildWii);
    AddPage(tabs, context, "Controls", [](PageBuilder& page) { BuildControls(page, true); });
  }
  else
  {
    AddPage(tabs, context, "GameCube", [game](PageBuilder& page) { BuildGameCube(page, game); });
    AddPage(tabs, context, "Controls", [](PageBuilder& page) { BuildControls(page, false); });
  }

  auto* frame = new brls::AppletFrame(tabs);
  frame->setTitle(GetTitle(*game));
  frame->registerAction("Reset to defaults", brls::BUTTON_Y, [context, tabs](brls::View*) {
    auto* dialog = new brls::Dialog("Remove every setting this game overrides?");
    dialog->addButton("Cancel", [] {});
    dialog->addButton("Reset", [context, tabs] {
      context->ClearAll();
      tabs->focusTab(0);
      brls::Application::notify("This game now follows the global settings.");
    });
    dialog->open();
    return true;
  });
  return new brls::Activity(frame);
}
}  // namespace Shell
