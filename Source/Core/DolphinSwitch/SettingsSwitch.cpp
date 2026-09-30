// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/SettingsSwitch.h"

#include "Common/CommonPaths.h"
#include "Common/Config/Layer.h"
#include "Common/FileUtil.h"
#include "Core/Config/GraphicsSettings.h"
#include "VideoCommon/VideoConfig.h"

namespace Config
{
const Info<PerformanceProfile> SWITCH_PERFORMANCE_PROFILE{
    {System::Main, "Switch", "PerformanceProfile"}, PerformanceProfile::FasterMemory};
const Info<int> SWITCH_PERFORMANCE_OVERLAY{{System::Main, "Switch", "PerformanceOverlay"}, 1};
const Info<std::string> SWITCH_GAME_DIRECTORY{{System::Main, "Switch", "GameDirectory"}, ""};
const Info<GameListSort> SWITCH_GAME_LIST_SORT{{System::Main, "Switch", "GameListSort"},
                                               GameListSort::Title};
const Info<GameListFilter> SWITCH_GAME_LIST_FILTER{{System::Main, "Switch", "GameListFilter"},
                                                   GameListFilter::All};
const std::array<Info<WiimoteLayout>, 4> SWITCH_WIIMOTE_LAYOUTS{
    Info<WiimoteLayout>{{System::Main, "Switch", "WiimoteLayout1"}, WiimoteLayout::Vertical},
    Info<WiimoteLayout>{{System::Main, "Switch", "WiimoteLayout2"}, WiimoteLayout::Vertical},
    Info<WiimoteLayout>{{System::Main, "Switch", "WiimoteLayout3"}, WiimoteLayout::Vertical},
    Info<WiimoteLayout>{{System::Main, "Switch", "WiimoteLayout4"}, WiimoteLayout::Vertical},
};
const std::array<Info<std::string>, 4> SWITCH_PAD_PROFILES{
    Info<std::string>{{System::Main, "Switch", "PadProfile1"}, ""},
    Info<std::string>{{System::Main, "Switch", "PadProfile2"}, ""},
    Info<std::string>{{System::Main, "Switch", "PadProfile3"}, ""},
    Info<std::string>{{System::Main, "Switch", "PadProfile4"}, ""},
};
const std::array<Info<std::string>, 4> SWITCH_WIIMOTE_PROFILES{
    Info<std::string>{{System::Main, "Switch", "WiimoteProfile1"}, ""},
    Info<std::string>{{System::Main, "Switch", "WiimoteProfile2"}, ""},
    Info<std::string>{{System::Main, "Switch", "WiimoteProfile3"}, ""},
    Info<std::string>{{System::Main, "Switch", "WiimoteProfile4"}, ""},
};
}  // namespace Config

namespace SwitchSettings
{
namespace
{
template <typename T>
void SetBaseIfUnset(const Config::Info<T>& info, const T& value)
{
  if (!Config::GetLayer(Config::LayerType::Base)->Exists(info.GetLocation()))
    Config::SetBase(info, value);
}
}  // namespace

void ApplyDefaults()
{
  SetBaseIfUnset(Config::GFX_SHADER_COMPILATION_MODE,
                 ShaderCompilationMode::AsynchronousSkipRendering);
  SetBaseIfUnset(Config::GFX_SHADER_COMPILER_THREADS, -1);
  SetBaseIfUnset(Config::GFX_SHADER_PRECOMPILER_THREADS, -1);
  SetBaseIfUnset(Config::GFX_ENHANCE_MAX_ANISOTROPY, AnisotropicFilteringMode::Force1x);
  SetBaseIfUnset(Config::GFX_ENABLE_GPU_TEXTURE_DECODING, true);
}

std::string GetGameDirectory()
{
  std::string directory = Config::Get(Config::SWITCH_GAME_DIRECTORY);
  if (directory.empty())
    return File::GetUserPath(D_USER_IDX) + "roms" DIR_SEP;

  if (directory.back() != DIR_SEP_CHR)
    directory += DIR_SEP_CHR;
  return directory;
}

u32 GetPerformanceConfiguration()
{
  switch (Config::Get(Config::SWITCH_PERFORMANCE_PROFILE))
  {
  case Config::PerformanceProfile::Stock:
    // CPU 1020, GPU 307.2, RAM 1331.2.
    return 0x00020003;
  case Config::PerformanceProfile::FasterMemoryAndGpu:
    // CPU 1020, GPU 460.8, RAM 1600.
    return 0x92220007;
  case Config::PerformanceProfile::FasterMemory:
  default:
    // CPU 1020, GPU 307.2, RAM 1600.
    return 0x00020001;
  }
}
}  // namespace SwitchSettings
