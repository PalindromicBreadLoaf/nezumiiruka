// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HorizonClocks.h"

#include <mutex>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common::HorizonClocks
{
namespace
{
constexpr u32 HANDHELD_CONFIGURATION = 0x00020001;

std::mutex s_mutex;
u32 s_original_configuration = 0;
int s_boost_count = 0;

bool IsApplication()
{
  return appletGetAppletType() == AppletType_Application;
}
}  // namespace

void ApplyPerformanceConfiguration()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (R_FAILED(
          apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &s_original_configuration)))
    s_original_configuration = 0;

  const Result rc =
      apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, HANDHELD_CONFIGURATION);
  if (R_FAILED(rc))
  {
    WARN_LOG_FMT(COMMON, "apmSetPerformanceConfiguration({:#010x}) failed: {:#010x}",
                 HANDHELD_CONFIGURATION, rc);
    return;
  }
  NOTICE_LOG_FMT(COMMON, "Requested performance configuration {:#010x}", HANDHELD_CONFIGURATION);
}

void RestorePerformanceConfiguration()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_original_configuration != 0)
    apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, s_original_configuration);
}

void AcquireCpuBoost()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count++ != 0)
    return;

  const Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
  if (R_FAILED(rc))
    WARN_LOG_FMT(COMMON, "appletSetCpuBoostMode(FastLoad) failed: {:#010x}", rc);
  else
    NOTICE_LOG_FMT(COMMON, "CPU boost on");
}

void ReleaseCpuBoost()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count == 0 || --s_boost_count != 0)
    return;

  const Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  if (R_FAILED(rc))
    WARN_LOG_FMT(COMMON, "appletSetCpuBoostMode(Normal) failed: {:#010x}", rc);
  else
    NOTICE_LOG_FMT(COMMON, "CPU boost off");
}
}  // namespace Common::HorizonClocks
