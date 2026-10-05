// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Shell
{
void StartUpdaterOnLaunch();

void CheckForUpdates();
void ShowReleaseNotes();

void StopUpdaterTasks();
}  // namespace Shell
