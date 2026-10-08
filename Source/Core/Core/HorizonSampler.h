// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <string>

namespace Core
{
class System;
}

namespace Core::HorizonSampler
{
void RegisterCpuThread();
void UnregisterCpuThread();

bool IsRunning();

void Poll(Core::System& system);

void Start(Core::System& system, double seconds, std::string host_description);
void Stop();
}  // namespace Core::HorizonSampler

#endif
