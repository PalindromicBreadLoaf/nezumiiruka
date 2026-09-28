// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HorizonClocks.h"

#include <mutex>
#include <optional>

#include <switch.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common::HorizonClocks
{
namespace
{
std::mutex s_mutex;
std::optional<u32> s_original_configuration;
int s_boost_count = 0;

bool IsApplication()
{
  return appletGetAppletType() == AppletType_Application;
}
}  // namespace

void ApplyPerformanceConfiguration(u32 configuration)
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (!s_original_configuration)
  {
    u32 original = 0;
    if (R_SUCCEEDED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &original)))
      s_original_configuration = original;
    else
      s_original_configuration = 0;
  }

  const Result rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, configuration);
  if (R_FAILED(rc))
  {
    WARN_LOG_FMT(COMMON, "apmSetPerformanceConfiguration({:#010x}) failed: {:#010x}", configuration,
                 rc);
    return;
  }
  NOTICE_LOG_FMT(COMMON, "Requested performance configuration {:#010x}", configuration);
}

void RestorePerformanceConfiguration()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_original_configuration.value_or(0) != 0)
    apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, *s_original_configuration);
  s_original_configuration.reset();
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
