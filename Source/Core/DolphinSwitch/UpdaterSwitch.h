// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <atomic>
#include <expected>
#include <functional>
#include <string>
#include <string_view>

#include "Common/CommonTypes.h"

namespace UpdaterSwitch
{
struct Release
{
  std::string tag;
  std::string notes;
  std::string download_url;
  std::string sha256;
  u64 size = 0;
  bool prerelease = false;
};

enum class CheckStatus
{
  Available,
  UpToDate,
  Error,
};

struct CheckResult
{
  CheckStatus status = CheckStatus::Error;
  Release release;
  std::string current_notes;
  std::string error;
};

struct ReleaseNotes
{
  std::string tag;
  std::string text;
};

using ProgressFn = std::function<void(u64 downloaded, u64 total)>;

const char* GetCurrentVersion();

void SetExecutablePath(std::string path);
const std::string& GetExecutablePath();

int CompareVersions(std::string_view lhs, std::string_view rhs);

CheckResult CheckForUpdate(bool include_prereleases, const std::atomic_bool& cancelled);

std::expected<void, std::string> Install(const Release& release, const std::atomic_bool& cancelled,
                                         const ProgressFn& progress);

bool CanRelaunch();
void QueueRelaunch();

ReleaseNotes LoadCachedNotes();
void CacheNotes(const std::string& tag, const std::string& text);
}  // namespace UpdaterSwitch
