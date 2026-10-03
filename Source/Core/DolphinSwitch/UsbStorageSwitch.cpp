// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/UsbStorageSwitch.h"

#include <atomic>
#include <mutex>

#include <switch.h>
#include <usbhsfs.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "DolphinSwitch/SettingsSwitch.h"

namespace UsbStorage
{
namespace
{
std::mutex s_volumes_mutex;
std::vector<Volume> s_volumes;
std::atomic_bool s_changed = false;
bool s_initialised = false;

Volume ToVolume(const UsbHsFsDevice& device)
{
  return {.root = std::string(device.name) + DIR_SEP,
          .label = device.product_name[0] != '\0' ? device.product_name : device.name};
}

void StoreVolumes(std::vector<Volume> volumes)
{
  {
    std::lock_guard lock(s_volumes_mutex);
    s_volumes = std::move(volumes);
  }
  s_changed = true;
}

void OnPopulate(const UsbHsFsDevice* devices, u32 device_count, void*)
{
  std::vector<Volume> volumes;
  for (u32 i = 0; devices != nullptr && i < device_count; ++i)
  {
    volumes.push_back(ToVolume(devices[i]));
    NOTICE_LOG_FMT(COMMON, "USB drive {} mounted as {}", volumes.back().label, volumes.back().root);
  }
  StoreVolumes(std::move(volumes));
}

void ListMountedVolumes()
{
  const u32 count = usbHsFsGetMountedDeviceCount();
  std::vector<UsbHsFsDevice> devices(count);
  const u32 listed = count != 0 ? usbHsFsListMountedDevices(devices.data(), count) : 0;
  OnPopulate(devices.data(), listed, nullptr);
}
}  // namespace

void Init()
{
  if (s_initialised)
    return;

  usbHsFsSetFileSystemMountFlags(UsbHsFsMountFlags_ReplayJournal);

  const Result rc = usbHsFsInitialize(0);
  if (R_FAILED(rc))
  {
    WARN_LOG_FMT(COMMON, "USB storage unavailable ({:#010x})", rc);
    return;
  }

  s_initialised = true;
  usbHsFsSetPopulateCallback(OnPopulate, nullptr);
  ListMountedVolumes();
  s_changed = false;
}

void Shutdown()
{
  if (!s_initialised)
    return;

  usbHsFsSetPopulateCallback(nullptr, nullptr);
  usbHsFsExit();
  s_initialised = false;

  std::lock_guard lock(s_volumes_mutex);
  s_volumes.clear();
}

std::vector<Volume> GetVolumes()
{
  std::lock_guard lock(s_volumes_mutex);
  return s_volumes;
}

bool ConsumeChange()
{
  return s_changed.exchange(false);
}

std::vector<std::string> GetGameDirectories()
{
  std::string folder = Config::Get(Config::SWITCH_USB_GAME_DIRECTORY);
  while (!folder.empty() && folder.front() == DIR_SEP_CHR)
    folder.erase(folder.begin());
  if (!folder.empty() && folder.back() != DIR_SEP_CHR)
    folder += DIR_SEP_CHR;

  std::vector<std::string> directories;
  for (const Volume& volume : GetVolumes())
  {
    std::string directory = volume.root + folder;
    if (File::IsDirectory(directory))
      directories.push_back(std::move(directory));
  }
  return directories;
}
}  // namespace UsbStorage
