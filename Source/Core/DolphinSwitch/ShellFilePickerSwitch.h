// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace brls
{
class Activity;
}

namespace Shell
{
using FileChosenFn = std::function<void(const std::string& path)>;

brls::Activity* CreateFilePickerActivity(const std::string& title, const std::string& directory,
                                         std::vector<std::string> extensions,
                                         FileChosenFn on_chosen);
}  // namespace Shell
