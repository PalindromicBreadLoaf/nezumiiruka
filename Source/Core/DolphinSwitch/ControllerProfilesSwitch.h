// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Common/Config/Config.h"

namespace ControllerEmu
{
class EmulatedController;
}

namespace ControllerProfiles
{
enum class Kind
{
  GCPad,
  Wiimote,
};

constexpr int SLOT_COUNT = 4;

std::string GetKindLabel(Kind kind);

std::vector<std::string> GetPresetNames(Kind kind);
std::vector<std::string> GetUserProfileNames(Kind kind);
bool IsPreset(Kind kind, const std::string& name);
bool Exists(Kind kind, const std::string& name);
std::string GetDefaultProfile(Kind kind);

std::string GetPreset(Kind kind, const std::string& name);

std::string CheckNewName(Kind kind, const std::string& name);

bool Create(Kind kind, const std::string& name, const std::string& source);
bool Rename(Kind kind, const std::string& old_name, const std::string& new_name);
bool Delete(Kind kind, const std::string& name);

std::unique_ptr<ControllerEmu::EmulatedController> CreateController(Kind kind);
bool Load(Kind kind, const std::string& name, ControllerEmu::EmulatedController& controller);
bool Save(Kind kind, const std::string& name, ControllerEmu::EmulatedController& controller);

std::string GetGlobalProfile(Kind kind, int slot);
void SetGlobalProfile(Kind kind, int slot, const std::string& name);
const Config::Info<std::string>& GetGameProfileInfo(Kind kind, int slot);

void WritePresets();

void ApplyProfiles();
}  // namespace ControllerProfiles
