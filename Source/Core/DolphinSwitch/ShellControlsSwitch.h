// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <string>

#include "DolphinSwitch/ControllerProfilesSwitch.h"

namespace brls
{
class Activity;
class View;
}  // namespace brls

namespace Shell
{
struct ProfileBinding
{
  std::function<std::string()> current;
  std::function<void(const std::string&)> write;

  std::function<std::string()> inherited;
  std::function<bool()> overridden;
  std::function<void()> clear;
};

brls::View* CreateProfileCell(const std::string& title, ControllerProfiles::Kind kind,
                              ProfileBinding binding);

brls::Activity* CreateProfileManagerActivity(ControllerProfiles::Kind kind);
}  // namespace Shell
