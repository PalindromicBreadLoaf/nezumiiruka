// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/WiimoteProfilesSwitch.h"

#include <initializer_list>
#include <memory>

#include <fmt/format.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/IniFile.h"
#include "Common/Logging/Log.h"
#include "Core/HW/Wiimote.h"
#include "Core/HW/WiimoteEmu/Extension/Classic.h"
#include "Core/HW/WiimoteEmu/Extension/Nunchuk.h"
#include "Core/HW/WiimoteEmu/ExtensionPort.h"
#include "Core/HW/WiimoteEmu/WiimoteEmu.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"

namespace WiimoteProfiles
{
namespace
{
using Config::WiimoteLayout;
using WiimoteEmu::ClassicGroup;
using WiimoteEmu::NunchukGroup;
using WiimoteEmu::WiimoteGroup;

const std::array<Config::Info<std::string>, 4> GAME_PROFILE_INFOS{
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile1"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile2"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile3"},
                              ""},
    Config::Info<std::string>{{Config::System::GameSettingsOnly, "Controls", "WiimoteProfile4"},
                              ""},
};

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

void SaveLayout(WiimoteLayout layout, const std::string& device, Common::IniFile::Section* section)
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

  if (!device.empty())
    wiimote.SetDefaultDevice(device);

  wiimote.SaveConfig(section);
}

std::string GetProfileDirectory()
{
  return File::GetUserPath(D_CONFIG_IDX) + "Profiles/Wiimote/";
}
}  // namespace

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

std::string GetProfileName(WiimoteLayout layout)
{
  return "Switch - " + GetLayoutLabel(layout);
}

std::optional<WiimoteLayout> FindLayout(const std::string& profile_name)
{
  for (const WiimoteLayout layout : LAYOUTS)
  {
    if (GetProfileName(layout) == profile_name)
      return layout;
  }
  return std::nullopt;
}

const Config::Info<std::string>& GetGameProfileInfo(int index)
{
  return GAME_PROFILE_INFOS.at(index);
}

void WriteProfiles()
{
  const std::string directory = GetProfileDirectory();
  File::CreateFullPath(directory);

  for (const WiimoteLayout layout : LAYOUTS)
  {
    Common::IniFile ini;
    SaveLayout(layout, "", ini.GetOrCreateSection("Profile"));

    const std::string path = directory + GetProfileName(layout) + ".ini";
    if (!ini.Save(path))
      ERROR_LOG_FMT(CONTROLLERINTERFACE, "Could not write the Wii Remote profile {}", path);
  }
}

void ApplyLayouts()
{
  const std::string path = File::GetUserPath(D_CONFIG_IDX) + WIIMOTE_INI_NAME ".ini";

  Common::IniFile ini;
  ini.Load(path);

  for (int index = 0; index < static_cast<int>(Config::SWITCH_WIIMOTE_LAYOUTS.size()); ++index)
  {
    const std::string section = fmt::format("Wiimote{}", index + 1);
    ini.DeleteSection(section);
    SaveLayout(Config::Get(Config::SWITCH_WIIMOTE_LAYOUTS[index]),
               fmt::format("Horizon/0/Player {}", index + 1), ini.GetOrCreateSection(section));
  }

  if (!ini.Save(path))
    ERROR_LOG_FMT(CONTROLLERINTERFACE, "Could not write {}", path);
}
}  // namespace WiimoteProfiles
