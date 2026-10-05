// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ControllerProfilesSwitch.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/FileSearch.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Core/HW/GCPadEmu.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Classic.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/ExtensionPort.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ControllerProfiles
{
namespace
{
using Config::WiimoteLayout;
using WiimoteEmu::ClassicGroup;
using WiimoteEmu::NunchukGroup;
using WiimoteEmu::WiimoteGroup;

constexpr std::string_view PRESET_PREFIX = "Switch - ";
const std::string GC_PRESET = "Switch - GameCube controller";

constexpr std::array WIIMOTE_LAYOUTS{WiimoteLayout::Vertical, WiimoteLayout::Sideways,
                                     WiimoteLayout::SidewaysJoyCon, WiimoteLayout::Nunchuk,
                                     WiimoteLayout::Classic};

const std::array<Config::Info<std::string>, SLOT_COUNT> GAME_PAD_PROFILE_INFOS{
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "PadProfile1"}, ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "PadProfile2"}, ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "PadProfile3"}, ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "PadProfile4"}, ""},
};

const std::array<Config::Info<std::string>, SLOT_COUNT> GAME_WIIMOTE_PROFILE_INFOS{
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile1"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile2"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile3"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile4"},
                              ""},
};

const Config::Info<std::string>& GetGlobalProfileInfo(Kind kind, int slot)
{
  return kind == Kind::GCPad ? Config::SWITCH_PAD_PROFILES.at(slot) :
                               Config::SWITCH_WIIMOTE_PROFILES.at(slot);
}

std::string GetDirectory(Kind kind)
{
  return File::GetUserPath(D_CONFIG_IDX) +
         (kind == Kind::GCPad ? "Profiles/GCPad/" : "Profiles/Wiimote/");
}

std::string GetPath(Kind kind, const std::string& name)
{
  return GetDirectory(kind) + name + ".ini";
}

std::string GetLayoutLabel(WiimoteLayout layout)
{
  switch (layout)
  {
  case WiimoteLayout::Sideways:
    return "Sideways Wii Remote";
  case WiimoteLayout::SidewaysJoyCon:
    return "Sideways Joy-Con";
  case WiimoteLayout::Nunchuk:
    return "Wii Remote and Nunchuk";
  case WiimoteLayout::Classic:
    return "Classic Controller";
  case WiimoteLayout::Vertical:
  default:
    return "Wii Remote";
  }
}

std::string GetLayoutProfileName(WiimoteLayout layout)
{
  return std::string(PRESET_PREFIX) + GetLayoutLabel(layout);
}

void Bind(ControllerEmu::ControlGroup* group, std::initializer_list<const char*> expressions)
{
  int index = 0;
  for (const char* expression : expressions)
    group->SetControlExpression(index++, expression);
}

void BindShake(ControllerEmu::ControlGroup* group, const char* expression)
{
  Bind(group, {expression, expression, expression});
}

void BindCommon(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::Point),
       {"`Horizon/0/Touchscreen:Touch Y-`", "`Horizon/0/Touchscreen:Touch Y+`",
        "`Horizon/0/Touchscreen:Touch X-`", "`Horizon/0/Touchscreen:Touch X+`"});

  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::Rumble), {"`Motor`"});
}

void BindVerticalMotion(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::IMUAccelerometer),
       {"`Accel Up`", "`Accel Down`", "`Accel Left`", "`Accel Right`", "`Accel Forward`",
        "`Accel Backward`"});
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::IMUGyroscope),
       {"`Gyro Pitch Up`", "`Gyro Pitch Down`", "`Gyro Roll Left`", "`Gyro Roll Right`",
        "`Gyro Yaw Left`", "`Gyro Yaw Right`"});
}

void BindSidewaysMotion(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::IMUAccelerometer),
       {"`Sideways Accel Up`", "`Sideways Accel Down`", "`Sideways Accel Left`",
        "`Sideways Accel Right`", "`Sideways Accel Forward`", "`Sideways Accel Backward`"});
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::IMUGyroscope),
       {"`Sideways Gyro Pitch Up`", "`Sideways Gyro Pitch Down`", "`Sideways Gyro Roll Left`",
        "`Sideways Gyro Roll Right`", "`Sideways Gyro Yaw Left`", "`Sideways Gyro Yaw Right`"});
}

void BindRecenter(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::IMUPoint), {"`Stick L`"});
}

void BindVertical(WiimoteEmu::Wiimote& wiimote, bool stick_as_dpad)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::Buttons),
       {"`A`", "`ZR` | `B`", "`X`", "`Y`", "`Minus`", "`Plus`", "`Stick R`"});

  if (stick_as_dpad)
  {
    Bind(wiimote.GetWiimoteGroup(WiimoteGroup::DPad),
         {"`Pad Up` | `Left Y+`", "`Pad Down` | `Left Y-`", "`Pad Left` | `Left X-`",
          "`Pad Right` | `Left X+`"});
  }
  else
  {
    Bind(wiimote.GetWiimoteGroup(WiimoteGroup::DPad),
         {"`Pad Up`", "`Pad Down`", "`Pad Left`", "`Pad Right`"});
  }

  BindShake(wiimote.GetWiimoteGroup(WiimoteGroup::Shake), "`R`");
  BindVerticalMotion(wiimote);
  BindRecenter(wiimote);
}

void BindSideways(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::Buttons),
       {"`L`", "`R` | `ZR`", "`Y` | `X`", "`B` | `A`", "`Minus`", "`Plus`", "`Stick R`"});

  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::DPad),
       {"`Pad Left` | `Left X-`", "`Pad Right` | `Left X+`", "`Pad Down` | `Left Y-`",
        "`Pad Up` | `Left Y+`"});

  BindShake(wiimote.GetWiimoteGroup(WiimoteGroup::Shake), "`ZL`");
  BindSidewaysMotion(wiimote);
  BindRecenter(wiimote);
}

void BindSidewaysJoyCon(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::Buttons),
       {"`SL`", "`SR`", "`Sideways Left` | `Sideways Up`", "`Sideways Down` | `Sideways Right`", "",
        "`Sideways Menu`", "`Sideways Stick Click`"});

  Bind(wiimote.GetWiimoteGroup(WiimoteGroup::DPad), {"`Sideways Stick X-`", "`Sideways Stick X+`",
                                                     "`Sideways Stick Y-`", "`Sideways Stick Y+`"});

  BindSidewaysMotion(wiimote);
}

void BindNunchuk(WiimoteEmu::Wiimote& wiimote)
{
  BindVertical(wiimote, false);

  Bind(wiimote.GetNunchukGroup(NunchukGroup::Buttons), {"`L`", "`ZL`"});
  Bind(wiimote.GetNunchukGroup(NunchukGroup::Stick),
       {"`Left Y+`", "`Left Y-`", "`Left X-`", "`Left X+`"});
  BindShake(wiimote.GetNunchukGroup(NunchukGroup::Shake),
            "`Right X-` | `Right X+` | `Right Y-` | `Right Y+`");
}

void BindClassic(WiimoteEmu::Wiimote& wiimote)
{
  Bind(wiimote.GetClassicGroup(ClassicGroup::Buttons),
       {"`A`", "`B`", "`X`", "`Y`", "`ZL`", "`ZR`", "`Minus`", "`Plus`", "`Stick R`"});

  Bind(wiimote.GetClassicGroup(ClassicGroup::Triggers), {"`L`", "`R`", "`L`", "`R`"});

  Bind(wiimote.GetClassicGroup(ClassicGroup::DPad),
       {"`Pad Up`", "`Pad Down`", "`Pad Left`", "`Pad Right`"});
  Bind(wiimote.GetClassicGroup(ClassicGroup::LeftStick),
       {"`Left Y+`", "`Left Y-`", "`Left X-`", "`Left X+`"});
  Bind(wiimote.GetClassicGroup(ClassicGroup::RightStick),
       {"`Right Y+`", "`Right Y-`", "`Right X-`", "`Right X+`"});
}

WiimoteEmu::ExtensionNumber GetExtension(WiimoteLayout layout)
{
  switch (layout)
  {
  case WiimoteLayout::Nunchuk:
    return WiimoteEmu::ExtensionNumber::NUNCHUK;
  case WiimoteLayout::Classic:
    return WiimoteEmu::ExtensionNumber::CLASSIC;
  default:
    return WiimoteEmu::ExtensionNumber::NONE;
  }
}

void SaveWiimoteLayout(WiimoteLayout layout, Common::IniFile::Section* section)
{
  WiimoteEmu::Wiimote wiimote(0);

  BindCommon(wiimote);
  switch (layout)
  {
  case WiimoteLayout::Sideways:
    BindSideways(wiimote);
    break;
  case WiimoteLayout::SidewaysJoyCon:
    BindSidewaysJoyCon(wiimote);
    break;
  case WiimoteLayout::Nunchuk:
    BindNunchuk(wiimote);
    break;
  case WiimoteLayout::Classic:
    BindClassic(wiimote);
    break;
  case WiimoteLayout::Vertical:
  default:
    BindVertical(wiimote, true);
    break;
  }

  static_cast<ControllerEmu::Attachments*>(wiimote.GetWiimoteGroup(WiimoteGroup::Attachments))
      ->SetSelectedAttachment(GetExtension(layout));

  wiimote.SaveConfig(section);
}

void SaveGCPadPreset(Common::IniFile::Section* section)
{
  GCPad pad(0);
  pad.LoadDefaults(g_controller_interface);
  pad.SetDefaultDevice("");
  pad.SaveConfig(section);
}

bool HasInvalidCharacter(const std::string& name)
{
  return name.find_first_of("/\\:*?\"<>|") != std::string::npos;
}

void ReplaceReferences(Kind kind, const std::string& old_name, const std::string& new_name)
{
  for (int slot = 0; slot < SLOT_COUNT; ++slot)
  {
    const Config::Info<std::string>& info = GetGlobalProfileInfo(kind, slot);
    if (Config::GetBase(info) == old_name)
      Config::SetBase(info, new_name);
  }

  const std::string key_prefix = kind == Kind::GCPad ? "PadProfile" : "WiimoteProfile";
  for (const std::string& path :
       Common::DoFileSearch(File::GetUserPath(D_GAMESETTINGS_IDX), ".ini", false))
  {
    Common::IniFile ini;
    if (!ini.Load(path))
      continue;

    Common::IniFile::Section* section = ini.GetSection("Controls");
    if (!section)
      continue;

    bool changed = false;
    for (int slot = 0; slot < SLOT_COUNT; ++slot)
    {
      const std::string key = fmt::format("{}{}", key_prefix, slot + 1);
      std::string value;
      if (!section->Get(key, &value) || value != old_name)
        continue;

      if (new_name.empty())
        section->Delete(key);
      else
        section->Set(key, new_name);
      changed = true;
    }

    if (changed && !ini.Save(path))
      ERROR_LOG_FMT(CONTROLLERINTERFACE, "Could not update the profile names in {}", path);
  }
}

void ApplyProfiles(Kind kind, const std::string& ini_name, const std::string& section_prefix)
{
  const std::string path = File::GetUserPath(D_CONFIG_IDX) + ini_name;

  Common::IniFile ini;
  ini.Load(path);

  for (int slot = 0; slot < SLOT_COUNT; ++slot)
  {
    const std::string section_name = fmt::format("{}{}", section_prefix, slot + 1);
    ini.DeleteSection(section_name);
    Common::IniFile::Section* section = ini.GetOrCreateSection(section_name);

    Common::IniFile profile;
    if (profile.Load(GetPath(kind, GetGlobalProfile(kind, slot))))
    {
      if (const Common::IniFile::Section* values = profile.GetSection("Profile"))
      {
        for (const auto& [key, value] : values->GetValues())
          section->Set(key, value);
      }
    }

    section->Set("Device", fmt::format("Horizon/0/Player {}", slot + 1));
  }

  if (!ini.Save(path))
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "Could not write {}", path);
}
}  // namespace

std::string GetKindLabel(Kind kind)
{
  return kind == Kind::GCPad ? "GameCube controller" : "Wii Remote";
}

std::vector<std::string> GetPresetNames(Kind kind)
{
  if (kind == Kind::GCPad)
    return {GC_PRESET};

  std::vector<std::string> names;
  for (const WiimoteLayout layout : WIIMOTE_LAYOUTS)
    names.push_back(GetLayoutProfileName(layout));
  return names;
}

std::vector<std::string> GetUserProfileNames(Kind kind)
{
  std::vector<std::string> names;
  for (const std::string& path : Common::DoFileSearch(GetDirectory(kind), ".ini", false))
  {
    std::string name;
    SplitPath(path, nullptr, &name, nullptr);
    if (!IsPreset(kind, name))
      names.push_back(std::move(name));
  }

  std::ranges::sort(names, Common::CaseInsensitiveLess{});
  return names;
}

bool IsPreset(Kind kind, const std::string& name)
{
  const std::vector<std::string> presets = GetPresetNames(kind);
  return std::ranges::find(presets, name) != presets.end();
}

bool Exists(Kind kind, const std::string& name)
{
  return !name.empty() && File::Exists(GetPath(kind, name));
}

std::string GetDefaultProfile(Kind kind)
{
  return GetPresetNames(kind).front();
}

std::string GetPreset(Kind kind, const std::string& name)
{
  if (IsPreset(kind, name))
    return name;

  Common::IniFile ini;
  std::string preset;
  if (ini.Load(GetPath(kind, name)) && !ini.GetOrCreateSection("Nezumiiruka")->Get("Preset", &preset))
    ini.GetOrCreateSection("Porpoise")->Get("Preset", &preset);

  return IsPreset(kind, preset) ? preset : GetDefaultProfile(kind);
}

std::string CheckNewName(Kind kind, const std::string& name)
{
  if (name.empty() || name.front() == ' ' || name.back() == ' ' || name.front() == '.')
    return "Profile names cannot be empty or start or end with a space.";
  if (HasInvalidCharacter(name))
    return "Profile names cannot contain / \\ : * ? \" < > or |.";
  if (name.starts_with(PRESET_PREFIX))
    return fmt::format("Names starting with \"{}\" are kept for presets.", PRESET_PREFIX);
  if (Exists(kind, name))
    return fmt::format("There is already a profile called \"{}\".", name);
  return {};
}

bool Create(Kind kind, const std::string& name, const std::string& source)
{
  Common::IniFile ini;
  if (!ini.Load(GetPath(kind, source)))
    return false;

  ini.GetOrCreateSection("Nezumiiruka")->Set("Preset", GetPreset(kind, source));
  ini.DeleteSection("Porpoise");
  File::CreateFullPath(GetDirectory(kind));
  return ini.Save(GetPath(kind, name));
}

bool Rename(Kind kind, const std::string& old_name, const std::string& new_name)
{
  if (!File::Rename(GetPath(kind, old_name), GetPath(kind, new_name)))
    return false;

  ReplaceReferences(kind, old_name, new_name);
  return true;
}

bool Delete(Kind kind, const std::string& name)
{
  if (!File::Delete(GetPath(kind, name)))
    return false;

  ReplaceReferences(kind, name, "");
  return true;
}

std::unique_ptr<ControllerEmu::EmulatedController> CreateController(Kind kind)
{
  if (kind == Kind::GCPad)
    return std::make_unique<GCPad>(0);
  return std::make_unique<WiimoteEmu::Wiimote>(0);
}

bool Load(Kind kind, const std::string& name, ControllerEmu::EmulatedController& controller)
{
  Common::IniFile ini;
  if (!ini.Load(GetPath(kind, name)))
    return false;

  controller.LoadConfig(ini.GetOrCreateSection("Profile"));
  return true;
}

bool Save(Kind kind, const std::string& name, ControllerEmu::EmulatedController& controller)
{
  const std::string path = GetPath(kind, name);

  Common::IniFile ini;
  ini.Load(path);
  ini.DeleteSection("Profile");

  Common::IniFile::Section* section = ini.GetOrCreateSection("Profile");
  controller.SaveConfig(section);
  section->Delete("Device");

  return ini.Save(path);
}

std::string GetGlobalProfile(Kind kind, int slot)
{
  std::string name = Config::Get(GetGlobalProfileInfo(kind, slot));

  if (name.empty() && kind == Kind::Wiimote)
    name = GetLayoutProfileName(Config::Get(Config::SWITCH_WIIMOTE_LAYOUTS.at(slot)));

  return Exists(kind, name) ? name : GetDefaultProfile(kind);
}

void SetGlobalProfile(Kind kind, int slot, const std::string& name)
{
  Config::SetBase(GetGlobalProfileInfo(kind, slot), name);
}

const Config::Info<std::string>& GetGameProfileInfo(Kind kind, int slot)
{
  return kind == Kind::GCPad ? GAME_PAD_PROFILE_INFOS.at(slot) :
                               GAME_WIIMOTE_PROFILE_INFOS.at(slot);
}

void WritePresets()
{
  const auto write = [](Kind kind, const std::string& name, const auto& fill) {
    Common::IniFile ini;
    fill(ini.GetOrCreateSection("Profile"));

    const std::string path = GetPath(kind, name);
    if (!ini.Save(path))
      ERROR_LOG_FMT(CONTROLLERINTERFACE, "Could not write the controller profile {}", path);
  };

  File::CreateFullPath(GetDirectory(Kind::GCPad));
  write(Kind::GCPad, GC_PRESET, SaveGCPadPreset);

  File::CreateFullPath(GetDirectory(Kind::Wiimote));
  for (const WiimoteLayout layout : WIIMOTE_LAYOUTS)
  {
    write(Kind::Wiimote, GetLayoutProfileName(layout),
          [layout](Common::IniFile::Section* section) { SaveWiimoteLayout(layout, section); });
  }
}

void ApplyProfiles()
{
  ApplyProfiles(Kind::GCPad, GCPAD_CONFIG, "GCPad");
  ApplyProfiles(Kind::Wiimote, WIIMOTE_INI_NAME ".ini", "Wiimote");
}
}  // namespace ControllerProfiles
