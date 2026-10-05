// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellSettingsSwitch.h"

#include <algorithm>
#include <cmath>
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
#include "DolphinSwitch/RiivolutionSwitch.h"
#include "DolphinSwitch/SettingsSwitch.h"
#ifdef USE_RETRO_ACHIEVEMENTS
#include "DolphinSwitch/ShellAchievementsSwitch.h"
#endif
#include "DolphinSwitch/ShellCheatsSwitch.h"
#include "DolphinSwitch/ShellControlsSwitch.h"
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

  void Header(const std::string& title, const std::string& subtitle = {})
  {
    auto* header = new brls::Header();
    header->setTitle(title);
    if (!subtitle.empty())
      header->setSubtitle(subtitle);
    header->setMarginTop(m_box->getChildren().empty() ? 0 : 24);
    m_box->addView(header);
  }

  void Note(const std::string& text)
  {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(16);
    label->setTextColor(brls::Application::getTheme()["brls/header/subtitle"]);
    label->setVerticalAlign(brls::VerticalAlign::TOP);
    label->setMargins(8, 16, 16, 16);
    m_box->addView(label);
  }

  void Toggle(const std::string& title, const Config::Info<bool>& info, bool inverted = false)
  {
    if (IsPerGame())
    {
      Choice(title, info, {{"Off", inverted}, {"On", !inverted}});
      return;
    }

    auto* cell = new brls::BooleanCell();
    cell->init(
        title, m_context->Read(info) != inverted,
        [context = m_context, &info, inverted](bool on) { context->Write(info, on != inverted); });
    m_box->addView(cell);
  }

  template <typename T>
  void Choice(const std::string& title, const Config::Info<T>& info, std::vector<Option<T>> options)
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

    Custom(title, std::move(labels), std::move(binding));
  }

  void Custom(const std::string& title, std::vector<std::string> labels, Binding binding)
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

      cell->init(title, labels, selected, [binding, option_count](int index) {
        if (index < option_count)
          binding.write(index);
      });
      m_box->addView(cell);
      return;
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

    cell->init(title, choices, selected, [binding, option_count, colour_detail](int index) {
      if (index == 0)
        binding.clear();
      else if (index <= option_count)
        binding.write(index - 1);
      colour_detail(index != 0);
    });
    colour_detail(selected != 0);
    m_box->addView(cell);
  }

  void Text(const std::string& title, const Config::Info<std::string>& info,
            const std::string& placeholder)
  {
    auto* cell = new brls::InputCell();
    cell->init(
        title, m_context->Read(info),
        [context = m_context, &info](std::string value) { context->Write(info, value); },
        placeholder, title, 256);
    m_box->addView(cell);
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

Binding OverclockBinding(const ContextPtr& context, const std::vector<float>& factors)
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
  binding.current = [context, index_of] {
    return index_of(context->Read(Config::MAIN_OVERCLOCK_ENABLE),
                    context->Read(Config::MAIN_OVERCLOCK));
  };
  binding.inherited = [context, index_of] {
    return index_of(context->ReadInherited(Config::MAIN_OVERCLOCK_ENABLE),
                    context->ReadInherited(Config::MAIN_OVERCLOCK));
  };
  binding.write = [context, factors](int index) {
    context->Write(Config::MAIN_OVERCLOCK_ENABLE, index != 0);
    if (index != 0)
      context->Write(Config::MAIN_OVERCLOCK, factors[index - 1]);
  };
  binding.overridden = [context] {
    return context->IsOverridden(Config::MAIN_OVERCLOCK_ENABLE.GetLocation()) ||
           context->IsOverridden(Config::MAIN_OVERCLOCK.GetLocation());
  };
  binding.clear = [context] {
    context->Clear(Config::MAIN_OVERCLOCK_ENABLE.GetLocation());
    context->Clear(Config::MAIN_OVERCLOCK.GetLocation());
  };
  return binding;
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
  page.Toggle("Enable cheats", Config::MAIN_ENABLE_CHEATS);
  if (!page.IsPerGame())
    page.Toggle("Track time played", Config::MAIN_TIME_TRACKING);
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
  page.Choice("Anti-aliasing", Config::GFX_MSAA, {{"Off", 1u}, {"2× MSAA", 2u}, {"4× MSAA", 4u}});
  page.Toggle("Scaled EFB copy", Config::GFX_HACK_COPY_EFB_SCALED);
  page.Toggle("Per-pixel lighting", Config::GFX_ENABLE_PIXEL_LIGHTING);
  page.Toggle("Force 24-bit colour", Config::GFX_ENHANCE_FORCE_TRUE_COLOR);
  page.Toggle("Disable fog", Config::GFX_DISABLE_FOG);
  page.Toggle("Disable copy filter", Config::GFX_ENHANCE_DISABLE_COPY_FILTER);
}

void BuildHacks(PageBuilder& page)
{
  page.Header("Embedded frame buffer");
  page.Toggle("Skip EFB access from CPU", Config::GFX_HACK_EFB_ACCESS_ENABLE, true);
  page.Toggle("Ignore format changes", Config::GFX_HACK_EFB_EMULATE_FORMAT_CHANGES, true);
  page.Toggle("Store EFB copies to texture only", Config::GFX_HACK_SKIP_EFB_COPY_TO_RAM);
  page.Toggle("Defer EFB copies to RAM", Config::GFX_HACK_DEFER_EFB_COPIES);
  page.Toggle("Defer EFB cache invalidation", Config::GFX_HACK_EFB_DEFER_INVALIDATION);
  page.Note("May result in a significant performance boost in some cases, but also may result in "
            "crashes.");

  page.Header("Texture cache");
  page.Choice("Accuracy", Config::GFX_SAFE_TEXTURE_CACHE_COLOR_SAMPLES,
              {{"Fast", 128}, {"Balanced", 512}, {"Safe", 0}});
  page.Toggle("GPU texture decoding", Config::GFX_ENABLE_GPU_TEXTURE_DECODING);
  page.Toggle("Fast texture sampling", Config::GFX_HACK_FAST_TEXTURE_SAMPLING);

  page.Header("External frame buffer");
  page.Toggle("Store XFB copies to texture only", Config::GFX_HACK_SKIP_XFB_COPY_TO_RAM);
  page.Toggle("Immediately present XFB", Config::GFX_HACK_IMMEDIATE_XFB);
  page.Toggle("Skip presenting duplicate frames", Config::GFX_HACK_SKIP_DUPLICATE_XFBS);

  page.Header("Other");
  page.Toggle("Fast depth calculation", Config::GFX_FAST_DEPTH_CALC);
  page.Toggle("Disable bounding box", Config::GFX_HACK_BBOX_ENABLE, true);
  page.Toggle("Vertex rounding", Config::GFX_HACK_VERTEX_ROUNDING);
  page.Toggle("VBI skip", Config::GFX_HACK_VI_SKIP);
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
  page.Custom("CPU clock override", {"Off", "50%", "75%", "90%", "110%", "125%", "150%", "200%"},
              OverclockBinding(page.GetContext(), {0.5f, 0.75f, 0.9f, 1.1f, 1.25f, 1.5f, 2.0f}));
  page.Note("Underclocking the emulated CPU can make demanding games reach full speed, at the "
            "risk of slowdown inside the game itself.");

  page.Header("Accuracy");
  page.Toggle("Enable MMU", Config::MAIN_MMU);
  page.Toggle("Enable FPRF", Config::MAIN_FPRF);
  page.Toggle("Accurate NaNs", Config::MAIN_ACCURATE_NANS);
  page.Toggle("Synchronise GPU thread", Config::MAIN_SYNC_GPU);
  page.Toggle("Synchronise on idle skipping", Config::MAIN_SYNC_ON_SKIP_IDLE);
  page.Toggle("Emulate disc speed", Config::MAIN_FAST_DISC_SPEED, true);

  page.Header("Timing");
  page.Toggle("Correct time drift", Config::MAIN_CORRECT_TIME_DRIFT);
  page.Toggle("Rush frame presentation", Config::MAIN_RUSH_FRAME_PRESENTATION);
  page.Toggle("Smooth early presentation", Config::MAIN_SMOOTH_EARLY_PRESENTATION);
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

  page.Header("Peripherals");
  page.Toggle("Insert SD card", Config::MAIN_WII_SD_CARD);
  page.Toggle("Allow writes to the SD card", Config::MAIN_ALLOW_SD_WRITES);
  page.Toggle("Connect USB keyboard", Config::MAIN_WII_KEYBOARD);

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

void BuildControls(PageBuilder& page, bool wii)
{
  using ControllerProfiles::Kind;

  if (!page.IsPerGame())
  {
    page.Header("Controllers");
    page.Action("Change grip and order", "", ShowControllerApplet);
  }

  page.Header("GameCube controller ports");
  for (int port = 0; port < ControllerProfiles::SLOT_COUNT; ++port)
  {
    if (!page.IsPerGame())
    {
      page.Choice(fmt::format("Port {}", port + 1), Config::GetInfoForSIDevice(port),
                  {{"Nothing", SerialInterface::SIDEVICE_NONE},
                   {"Standard controller", SerialInterface::SIDEVICE_GC_CONTROLLER}});
    }
    AddProfileCell(page, fmt::format("Port {} profile", port + 1), Kind::GCPad, port);
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
  page.Choice("Update channel", Config::SWITCH_UPDATE_CHANNEL,
              {{"Stable", Config::UpdateChannel::Stable},
               {"Beta", Config::UpdateChannel::Prerelease}});

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
  page.Note("Nezumiiruka is a port of the Dolphin emulator, licensed under the GNU GPL version 2 or "
            "later. The menus use borealis, licensed under the Apache License 2.0.");
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
      return CreatePage(
          context, [&](PageBuilder& page) { BuildGameCheats(page, game); }, false);
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
