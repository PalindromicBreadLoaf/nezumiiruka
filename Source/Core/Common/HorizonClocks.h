// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include "Common/CommonTypes.h"

namespace Common::HorizonClocks
{
void ApplyPerformanceConfiguration(u32 configuration);
void RestorePerformanceConfiguration();

void AcquireCpuBoost();
void ReleaseCpuBoost();

class ScopedCpuBoost
{
public:
  ScopedCpuBoost() { AcquireCpuBoost(); }
  ~ScopedCpuBoost() { ReleaseCpuBoost(); }

  ScopedCpuBoost(const ScopedCpuBoost&) = delete;
  ScopedCpuBoost& operator=(const ScopedCpuBoost&) = delete;
};
}  // namespace Common::HorizonClocks

#endif
