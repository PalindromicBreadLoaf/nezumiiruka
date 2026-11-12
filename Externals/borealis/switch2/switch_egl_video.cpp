// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "switch_egl_video.hpp"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

#include <borealis/core/application.hpp>
#include <borealis/core/logger.hpp>
#include <borealis/core/util.hpp>

#define NANOVG_GLES3_IMPLEMENTATION
#include <nanovg_gl.h>

namespace brls
{
namespace
{
constexpr int HANDHELD_WIDTH = 1280;
constexpr int HANDHELD_HEIGHT = 720;
constexpr int DOCKED_WIDTH = 1920;
constexpr int DOCKED_HEIGHT = 1080;
}  // namespace

SwitchEGLVideoContext::SwitchEGLVideoContext()
{
  m_resolution_event_ready =
      R_SUCCEEDED(appletGetDefaultDisplayResolutionChangeEvent(&m_resolution_event));
  if (!m_resolution_event_ready)
    Logger::warning("switch2: no display resolution change event, following the operation mode");

  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, nullptr, nullptr))
    fatal(fmt::format("switch2: eglInitialize failed: {:#x}", eglGetError()));
  m_display = display;

  if (!eglBindAPI(EGL_OPENGL_ES_API))
    fatal(fmt::format("switch2: eglBindAPI failed: {:#x}", eglGetError()));

  static constexpr EGLint CONFIG_ATTRIBUTES[] = {
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
      EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
      EGL_RED_SIZE,        8,
      EGL_GREEN_SIZE,      8,
      EGL_BLUE_SIZE,       8,
      EGL_ALPHA_SIZE,      8,
      EGL_DEPTH_SIZE,      24,
      EGL_STENCIL_SIZE,    8,
      EGL_NONE,
  };
  EGLConfig config = nullptr;
  EGLint config_count = 0;
  if (!eglChooseConfig(display, CONFIG_ATTRIBUTES, &config, 1, &config_count) || config_count == 0)
    fatal(fmt::format("switch2: no EGL config: {:#x}", eglGetError()));
  m_config = config;

  static constexpr EGLint CONTEXT_ATTRIBUTES[] = {
      EGL_CONTEXT_MAJOR_VERSION, 3,
      EGL_CONTEXT_MINOR_VERSION, 0,
      EGL_NONE,
  };
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, CONTEXT_ATTRIBUTES);
  if (context == EGL_NO_CONTEXT)
    fatal(fmt::format("switch2: eglCreateContext failed: {:#x}", eglGetError()));
  m_context = context;

  UpdateWindowSize();
  if (!CreateSurface())
    fatal(fmt::format("switch2: eglCreateWindowSurface failed: {:#x}", eglGetError()));

  m_nvg = nvgCreateGLES3(NVG_STENCIL_STROKES | NVG_ANTIALIAS);
  if (!m_nvg)
    fatal("switch2: unable to create the NanoVG GLES 3 context");

  setSwapInterval(VideoContext::swapInterval);
}

SwitchEGLVideoContext::~SwitchEGLVideoContext()
{
  EGLDisplay display = static_cast<EGLDisplay>(m_display);
  if (m_nvg)
    nvgDeleteGLES3(m_nvg);
  DestroySurface();
  if (m_context)
    eglDestroyContext(display, static_cast<EGLContext>(m_context));
  if (display)
    eglTerminate(display);
  if (m_resolution_event_ready)
    eventClose(&m_resolution_event);
}

void SwitchEGLVideoContext::UpdateWindowSize()
{
  s32 width = 0;
  s32 height = 0;
  if (R_FAILED(appletGetDefaultDisplayResolution(&width, &height)) || width <= 0 || height <= 0)
  {
    const bool docked = appletGetOperationMode() == AppletOperationMode_Console;
    width = docked ? DOCKED_WIDTH : HANDHELD_WIDTH;
    height = docked ? DOCKED_HEIGHT : HANDHELD_HEIGHT;
  }
  m_width = width;
  m_height = height;
  Application::onWindowResized(m_width, m_height);
}

bool SwitchEGLVideoContext::CreateSurface()
{
  NWindow* const window = nwindowGetDefault();
  nwindowSetDimensions(window, m_width, m_height);

  EGLDisplay display = static_cast<EGLDisplay>(m_display);
  EGLSurface surface = eglCreateWindowSurface(display, static_cast<EGLConfig>(m_config), window,
                                              nullptr);
  if (surface == EGL_NO_SURFACE)
    return false;
  m_surface = surface;
  return eglMakeCurrent(display, surface, surface, static_cast<EGLContext>(m_context));
}

void SwitchEGLVideoContext::DestroySurface()
{
  if (!m_surface)
    return;
  EGLDisplay display = static_cast<EGLDisplay>(m_display);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, static_cast<EGLSurface>(m_surface));
  m_surface = nullptr;
}

void SwitchEGLVideoContext::ResetSurface()
{
  DestroySurface();
  UpdateWindowSize();
  if (!CreateSurface())
    fatal(fmt::format("switch2: recreating the EGL surface failed: {:#x}", eglGetError()));
  setSwapInterval(VideoContext::swapInterval);
}

void SwitchEGLVideoContext::appletCallback(AppletHookType hook_type)
{
  if (hook_type == AppletHookType_OnOperationMode && !m_resolution_event_ready)
    ResetSurface();
}

void SwitchEGLVideoContext::clear(NVGcolor color)
{
  glClearColor(color.r, color.g, color.b, color.a);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
}

void SwitchEGLVideoContext::resetState()
{
  glDisable(GL_CULL_FACE);
  glDisable(GL_BLEND);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
}

void SwitchEGLVideoContext::beginFrame()
{
  if (m_resolution_event_ready && R_SUCCEEDED(eventWait(&m_resolution_event, 0)))
    ResetSurface();
  glViewport(0, 0, m_width, m_height);
}

void SwitchEGLVideoContext::endFrame()
{
  eglSwapBuffers(static_cast<EGLDisplay>(m_display), static_cast<EGLSurface>(m_surface));
}

void SwitchEGLVideoContext::setSwapInterval(int interval)
{
  VideoContext::swapInterval = interval;
  eglSwapInterval(static_cast<EGLDisplay>(m_display), interval);
}

double SwitchEGLVideoContext::getScaleFactor()
{
  return 1.0;
}

NVGcontext* SwitchEGLVideoContext::getNVGContext()
{
  return m_nvg;
}
}  // namespace brls
