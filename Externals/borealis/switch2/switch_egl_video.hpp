// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <switch.h>

#include <borealis/core/video.hpp>

namespace brls
{
// NanoVG over GLES 3
class SwitchEGLVideoContext : public VideoContext
{
public:
  SwitchEGLVideoContext();
  ~SwitchEGLVideoContext() override;

  void clear(NVGcolor color) override;
  void resetState() override;
  void beginFrame() override;
  void endFrame() override;
  void setSwapInterval(int interval) override;
  double getScaleFactor() override;
  NVGcontext* getNVGContext() override;

  void appletCallback(AppletHookType hook_type);

private:
  void UpdateWindowSize();
  bool CreateSurface();
  void DestroySurface();
  void ResetSurface();

  void* m_display = nullptr;
  void* m_config = nullptr;
  void* m_context = nullptr;
  void* m_surface = nullptr;

  ::Event m_resolution_event{};
  bool m_resolution_event_ready = false;

  int m_width = 1280;
  int m_height = 720;
  NVGcontext* m_nvg = nullptr;
};
}  // namespace brls
