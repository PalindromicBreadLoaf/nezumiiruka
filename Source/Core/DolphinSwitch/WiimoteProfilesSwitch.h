// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <optional>
#include <string>

#include "Common/Config/Config.h"
#include "DolphinSwitch/SettingsSwitch.h"

namespace WiimoteProfiles
{
constexpr std::array LAYOUTS{Config::WiimoteLayout::Vertical, Config::WiimoteLayout::Sideways,
                             Config::WiimoteLayout::SidewaysJoyCon, Config::WiimoteLayout::Nunchuk,
                             Config::WiimoteLayout::Classic};

std::string GetLayoutLabel(Config::WiimoteLayout layout);
std::string GetProfileName(Config::WiimoteLayout layout);
std::optional<Config::WiimoteLayout> FindLayout(const std::string& profile_name);

const Config::Info<std::string>& GetGameProfileInfo(int index);

void WriteProfiles();

void ApplyLayouts();
}  // namespace WiimoteProfiles
