// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>

#include "Common/CommonTypes.h"

namespace Shell
{
using NANDChangedFn = std::function<void()>;

bool IsSystemMenuInstalled();
std::string GetSystemMenuDescription();
bool IsTitleInstalled(u64 title_id);

void InstallWAD(const std::string& path, NANDChangedFn on_changed = {});
void ChooseAndInstallWAD(NANDChangedFn on_changed = {});
void UninstallTitle(u64 title_id, NANDChangedFn on_changed = {});

void PerformOnlineUpdate(NANDChangedFn on_changed = {});
void PerformDiscUpdate(const std::string& path, NANDChangedFn on_changed = {});

void ImportNANDBackup(NANDChangedFn on_changed = {});
void CheckNAND(NANDChangedFn on_changed = {});
void ExtractCertificates();

void ImportWiiSave();
void ExportWiiSaves();

void StopSystemTasks();
}  // namespace Shell
