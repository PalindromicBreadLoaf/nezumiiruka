// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <optional>
#include <vector>

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

#ifdef __SWITCH2__
enum class ClockDomain
{
  Cpu,
  Gpu,
  Memory,
  Count,
};

std::vector<u32> GetClockRates(ClockDomain domain);

void SetClockOverride(ClockDomain domain, u32 hz);
void ClearClockOverrides();

void MaintainClocks();

struct Telemetry
{
  u32 cpu_hz = 0;
  u32 gpu_hz = 0;
  u32 memory_hz = 0;
  std::optional<u32> cpu_microvolts;
  std::optional<u32> gpu_microvolts;
  std::optional<float> soc_celsius;
  std::optional<float> pcb_celsius;
};

std::optional<Telemetry> GetTelemetry();

void ShutdownClocks();
#endif
}  // namespace Common::HorizonClocks

#endif
