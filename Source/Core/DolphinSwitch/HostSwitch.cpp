// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

// The Host_* free functions the core calls back into.

#include "Core/Host.h"

#include <cstring>
#include <utility>

#include <switch.h>

#include "Common/Logging/Log.h"
#include "DolphinSwitch/PlatformSwitch.h"

#ifdef HAS_LIBMGBA
#include "DolphinSwitch/GBAOverlaySwitch.h"
#endif

std::vector<std::string> Host_GetPreferredLocales()
{
  if (R_FAILED(setInitialize()))
    return {};
  u64 language_code = 0;
  const Result rc = setGetSystemLanguage(&language_code);
  setExit();
  if (R_FAILED(rc))
    return {};

  char tag[sizeof(language_code) + 1] = {};
  std::memcpy(tag, &language_code, sizeof(language_code));
  return {tag};
}

void Host_PPCSymbolsChanged()
{
}

void Host_PPCBreakpointsChanged()
{
}

bool Host_UIBlocksControllerState()
{
  return g_platform && g_platform->IsMenuBlockingInput();
}

void Host_Message(const HostMessageID id)
{
  if (id == HostMessageID::WMUserStop && g_platform)
    g_platform->Stop();
}

void Host_UpdateTitle(const std::string& title)
{
  if (g_platform)
    g_platform->SetTitle(title);
}

void Host_UpdateDisasmDialog()
{
}

void Host_JitCacheInvalidation()
{
}

void Host_JitProfileDataWiped()
{
}

void Host_RequestRenderWindowSize(int width, int height)
{
  // The nwindow is whatever the dock state says it is.
}

bool Host_RendererHasFocus()
{
  return g_platform && g_platform->IsWindowFocused();
}

bool Host_RendererHasFullFocus()
{
  return Host_RendererHasFocus();
}

bool Host_RendererIsFullscreen()
{
  return true;
}

bool Host_TASInputHasFocus()
{
  return false;
}

void Host_YieldToUI()
{
}

void Host_TitleChanged()
{
}

void Host_UpdateDiscordClientID(const std::string& client_id)
{
}

bool Host_UpdateDiscordPresenceRaw(const std::string& details, const std::string& state,
                                   const std::string& large_image_key,
                                   const std::string& large_image_text,
                                   const std::string& small_image_key,
                                   const std::string& small_image_text,
                                   const int64_t start_timestamp, const int64_t end_timestamp,
                                   const int party_size, const int party_max)
{
  return false;
}

std::unique_ptr<GBAHostInterface> Host_CreateGBAHost(std::weak_ptr<HW::GBA::Core> core)
{
#ifdef HAS_LIBMGBA
  return GBAOverlay::CreateHost(std::move(core));
#else
  return nullptr;
#endif
}
