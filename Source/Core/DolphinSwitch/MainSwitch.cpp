// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <cstdio>
#include <memory>
#include <string>
#include <utility>

// struct in_addr for __nxlink_host.
#include <netinet/in.h>

#include <switch.h>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Config/Config.h"
#include "Common/FileUtil.h"
#include "Common/HorizonBuildId.h"
#include "Common/HorizonClocks.h"
#include "Common/HorizonFastmem.h"
#include "Common/HorizonJitStack.h"
#include "Common/HostCodeMemory.h"
#include "Common/Logging/Log.h"
#include "Common/ScopeGuard.h"
#include "Common/Thread.h"
#include "Common/Version.h"
#include "Core/AchievementManager.h"
#include "Core/Boot/Boot.h"
#include "Core/BootManager.h"
#include "Core/Config/GraphicsSettings.h"
#include "Core/Config/MainSettings.h"
#include "Core/Core.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"
#include "DolphinSwitch/ControllerProfilesSwitch.h"
#include "DolphinSwitch/PerformanceOverlaySwitch.h"
#include "DolphinSwitch/PlatformSwitch.h"
#include "DolphinSwitch/RiivolutionSwitch.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "DolphinSwitch/ShellSwitch.h"
#include "DolphinSwitch/UsbStorageSwitch.h"
#include "UICommon/UICommon.h"
#include "VideoCommon/VideoBackendBase.h"
#include "VideoCommon/VideoConfig.h"

std::unique_ptr<PlatformSwitch> g_platform;

namespace
{
// Applet mode is granted neither JIT capability nor enough memory to hold guest RAM,
// so it is worth raising an error on.
bool RunningAsApplication()
{
  switch (appletGetAppletType())
  {
  case AppletType_None:
  case AppletType_SystemApplet:
  case AppletType_LibraryApplet:
  case AppletType_OverlayApplet:
    return false;
  default:
    break;
  }

  u64 total_memory = 0;
  if (R_FAILED(svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0)))
    return false;

  return total_memory >= 1024ull * 1024 * 1024;
}

void RedirectStdioToNxlink()
{
  if (__nxlink_host.s_addr != 0)
    nxlinkStdio();
}

void LogHostEnvironment()
{
  const u32 core_mask = Common::GetAvailableCoreMask();

  u64 total_memory = 0;
  u64 used_memory = 0;
  svcGetInfo(&total_memory, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
  svcGetInfo(&used_memory, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);

  NOTICE_LOG_FMT(COMMON, "porpoise {}, build id {}", Common::GetScmDescStr(),
                 Common::HorizonBuildId::GetHex().data());
  NOTICE_LOG_FMT(COMMON, "Applet type {}, core mask {:#06b}, heap {} MiB used of {} MiB",
                 static_cast<int>(appletGetAppletType()), core_mask, used_memory / 0x100000,
                 total_memory / 0x100000);
  NOTICE_LOG_FMT(COMMON, "User directory {}", File::GetUserPath(D_USER_IDX));
  NOTICE_LOG_FMT(COMMON, "Sys directory {}", File::GetSysDirectory());

  // Properly log the current backend name.
  const std::string gfx_backend = Config::Get(Config::MAIN_GFX_BACKEND);
  std::string backend_name = VideoBackendBase::GetDefaultBackendDisplayName();
  for (const auto& backend : VideoBackendBase::GetAvailableBackends())
  {
    if (backend->GetConfigName() == gfx_backend)
    {
      backend_name = backend->GetDisplayName();
      break;
    }
  }
  NOTICE_LOG_FMT(COMMON, "Video backend {}", backend_name);
}

// BootManager::RestoreConfig() clears the CurrentRun layer when emulation ends, so these have to
// be reapplied for every boot.
void ApplyPlatformConfigOverrides()
{
  // Everything that emits host code shares one arena, so one failure covers all of them.
  // CachedInterpreter's code block is declared non-executable, so it allocates through
  // AllocateMemoryPages and runs without any of this.
  if (!Common::HostCodeMemory::IsAvailable())
  {
    WARN_LOG_FMT(COMMON, "No host code memory available. Falling back to the cached interpreter.");
    Config::SetCurrent(Config::MAIN_CPU_CORE, PowerPC::CPUCore::CachedInterpreter);
    Config::SetCurrent(Config::GFX_VERTEX_LOADER_TYPE, VertexLoaderType::Software);
  }

  // The CPU and GPU threads each get a dedicated core, so dual-core is mandatory here.
  Config::SetCurrent(Config::MAIN_CPU_THREAD, true);

  // The block cache's large entry point map wants 64 GiB of address space, which is twenty times
  // the application heap. There's no point even trying.
  Config::SetCurrent(Config::MAIN_LARGE_ENTRY_POINTS_MAP, false);

  const bool fastmem_arena = Common::HorizonFastmem::IsArenaSupported();
  Config::SetCurrent(Config::MAIN_FASTMEM_ARENA, fastmem_arena);

  // Page table fastmem additionally needs mappings where reads succeed and writes fault.
  Config::SetCurrent(Config::MAIN_PAGE_TABLE_FASTMEM,
                     fastmem_arena && Common::HorizonFastmem::AreReadOnlyMappingsSupported());

  Common::HorizonJitStack::GetGuardSource();

  // The overlay toggles live in the CurrentRun layer too, so the user's chosen level has to be
  // put back after every teardown.
  PerfOverlay::ApplyCurrentLevel();
}

void ConfigureInput(PadState& pad)
{
  padConfigureInput(8, HidNpadStyleSet_NpadStandard | HidNpadStyleTag_NpadGc);
  padInitializeDefault(&pad);

  hidSetNpadJoyHoldType(HidNpadJoyHoldType_Vertical);
}

std::unique_ptr<BootParameters> CreateBootParameters(const Shell::BootRequest& request)
{
  if (request.nand_title_id != 0)
    return std::make_unique<BootParameters>(BootParameters::NANDTitle{request.nand_title_id});

  auto boot = BootParameters::GenerateFromFile(request.path, BootSessionData{});
  if (boot && request.riivolution)
    RiivolutionSwitch::AddPatches(*boot);
  return boot;
}

std::string RunGame(PadState& pad, const Shell::BootRequest& request)
{
  ApplyPlatformConfigOverrides();
  Common::HorizonClocks::ApplyPerformanceConfiguration(
      SwitchSettings::GetPerformanceConfiguration());

  const std::string path = request.nand_title_id != 0 ?
                               fmt::format("NAND title {:016x}", request.nand_title_id) :
                               request.path;

  auto boot = CreateBootParameters(request);
  if (!boot)
  {
    ERROR_LOG_FMT(BOOT, "Could not read {}", path);
    return "Not a readable disc image.";
  }

  ConfigureInput(pad);
  ControllerProfiles::ApplyProfiles();

  g_platform =
      std::make_unique<PlatformSwitch>(pad, request.nand_title_id != 0 ? "" : request.path);
  Common::ScopeGuard platform_guard([] { g_platform.reset(); });

  if (!g_platform->Init())
  {
    ERROR_LOG_FMT(BOOT, "Could not acquire the window.");
    return "Could not acquire the window.";
  }

  const WindowSystemInfo wsi = g_platform->GetWindowSystemInfo();
  UICommon::InitControllers(wsi);
  Common::ScopeGuard controller_guard([] { UICommon::ShutdownControllers(); });

  // The core tearing itself down (a guest reset, or some sort of error) has to
  // break the run loop too.
  auto state_hook = Core::AddOnStateChangedCallback([](const Core::State state) {
    if (state == Core::State::Uninitialized && g_platform)
      g_platform->Stop();
  });

  auto& system = Core::System::GetInstance();
  if (!BootManager::BootCore(system, std::move(boot), wsi))
  {
    ERROR_LOG_FMT(BOOT, "Could not boot {}", path);
    return "Could not boot. Check the log.";
  }

  NOTICE_LOG_FMT(BOOT, "Booted {}", path);
  g_platform->MainLoop();

  Core::Stop(system);
  Core::Shutdown(system);
  NOTICE_LOG_FMT(BOOT, "Core shut down.");
  return {};
}
}  // namespace

int main(int argc, char* argv[])
{
  const bool have_socket = R_SUCCEEDED(socketInitializeDefault());
  if (have_socket)
    RedirectStdioToNxlink();
  Common::ScopeGuard socket_guard([have_socket] {
    if (have_socket)
      socketExit();
  });

  PadState pad;

  // Nothing is logged until UICommon::Init brings up LogManager, so this is the only marker if
  // something below goes wrong early.
  std::printf("porpoise %s\n", Common::GetScmDescStr().c_str());

  UICommon::SetUserDirectory("");
  UICommon::CreateDirectories();
  UICommon::Init();
  Common::ScopeGuard ui_common_guard([] { UICommon::Shutdown(); });

  SwitchSettings::ApplyDefaults();
  ControllerProfiles::WritePresets();
  File::CreateFullPath(SwitchSettings::GetGameDirectory());
  File::CreateFullPath(RiivolutionSwitch::GetPatchDirectory());

  LogHostEnvironment();

  UsbStorage::Init();
  Common::ScopeGuard usb_storage_guard([] { UsbStorage::Shutdown(); });

#ifdef USE_RETRO_ACHIEVEMENTS
  AchievementManager::GetInstance().Init(nullptr);
  Common::ScopeGuard achievements_guard([] { AchievementManager::GetInstance().Shutdown(); });
#endif

  Common::ScopeGuard clocks_guard([] { Common::HorizonClocks::RestorePerformanceConfiguration(); });

  Common::SetCurrentThreadName("Host thread");

  // The frontend/applet loop shares a core with audio and the background workers.
  Common::PinCurrentThreadToRole(Common::ThreadCoreRole::Host);

  // Reserved once for the whole session. Both JitArm64 and VertexLoaderARM64 carve out of this.
  Common::HostCodeMemory::Init();
  Common::ScopeGuard host_code_guard([] { Common::HostCodeMemory::Shutdown(); });

  Shell::BootRequest pending;
  if (argc > 1 && argv[1] != nullptr)
    pending.path = argv[1];
  std::string notice;

  if (!RunningAsApplication())
  {
    ERROR_LOG_FMT(COMMON, "Not running as an application.");
    notice = "WARNING: applet mode. Relaunch by holding R while starting a game.";
  }

  while (appletMainLoop())
  {
    Shell::BootRequest request = std::exchange(pending, {});

    if (request.IsEmpty())
      request = Shell::Run(notice);
    if (request.IsEmpty())
      break;

    notice = RunGame(pad, request);
  }

  return 0;
}
