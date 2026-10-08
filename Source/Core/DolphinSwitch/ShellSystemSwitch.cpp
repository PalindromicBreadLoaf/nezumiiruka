// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellSystemSwitch.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include <borealis.hpp>
#include <borealis/views/dropdown.hpp>
#include <fmt/format.h>

#include "Common/Align.h"
#include "Common/CommonPaths.h"
#include "Common/FatFsUtil.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/MsgHandler.h"
#include "Common/NandPaths.h"
#include "Common/Thread.h"
#include "Core/CommonTitles.h"
#include "Core/HW/WiiSave.h"
#include "Core/IOS/ES/Formats.h"
#include "Core/IOS/FS/FileSystem.h"
#include "Core/IOS/IOS.h"
#include "Core/WiiUtils.h"
#include "DiscIO/Enums.h"
#include "DiscIO/NANDImporter.h"
#include "DolphinSwitch/ShellFilePickerSwitch.h"

namespace Shell
{
namespace
{
constexpr u64 NAND_BIN_SIZE = 0x21000000;
constexpr u64 NAND_KEYS_SIZE = 0x400;

constexpr const char* SD_ROOT = "sdmc:/";

struct TaskState
{
  std::atomic_bool cancelled = false;

  brls::Label* label = nullptr;
  brls::Dialog* dialog = nullptr;
};

using TaskPtr = std::shared_ptr<TaskState>;
using TaskFn = std::function<std::function<void()>(const TaskPtr&)>;

std::thread s_thread;
TaskPtr s_task;

std::mutex s_alert_mutex;
std::string s_last_alert;

bool CaptureAlert(const char* caption, const char* text, bool yes_no, Common::MsgType style)
{
  std::fprintf(stderr, "%s\n", text);
  std::fflush(stderr);

  if (style == Common::MsgType::Warning || style == Common::MsgType::Critical)
  {
    std::lock_guard lock(s_alert_mutex);
    s_last_alert = text;
  }
  return false;
}

std::string TakeLastAlert()
{
  std::lock_guard lock(s_alert_mutex);
  return std::exchange(s_last_alert, {});
}

void ShowMessage(const std::string& text)
{
  auto* dialog = new brls::Dialog(text);
  dialog->addButton("OK", [] {});
  dialog->open();
}

void Confirm(const std::string& text, const std::string& action, std::function<void()> confirmed)
{
  auto* dialog = new brls::Dialog(text);
  dialog->addButton("Cancel", [] {});
  dialog->addButton(action, [confirmed = std::move(confirmed)] { brls::sync(confirmed); });
  dialog->open();
}

void NotifyChanged(const NANDChangedFn& on_changed)
{
  if (on_changed)
    on_changed();
}

void SetProgressText(const TaskPtr& task, std::string text)
{
  brls::sync([task, text = std::move(text)] {
    if (task->label && !task->cancelled)
      task->label->setText(text);
  });
}

void StartTask(const std::string& text, bool cancellable, TaskFn work)
{
  if (s_thread.joinable())
    s_thread.join();

  static std::once_flag s_alert_handler_registered;
  std::call_once(s_alert_handler_registered, [] { Common::RegisterMsgAlertHandler(CaptureAlert); });
  TakeLastAlert();

  auto task = std::make_shared<TaskState>();

  task->label = new brls::Label();
  task->label->setText(cancellable ? text + "\n\nPress B to cancel." : text);
  task->label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
  task->label->setMargins(32, 32, 32, 32);

  auto* content = new brls::Box(brls::Axis::COLUMN);
  content->setFocusable(true);
  content->setHideHighlight(true);
  content->addView(task->label);

  task->dialog = new brls::Dialog(content);
  task->dialog->setCancelable(false);
  if (cancellable)
  {
    task->dialog->registerAction("Cancel", brls::BUTTON_B, [task](brls::View*) {
      if (!task->cancelled.exchange(true) && task->label)
        task->label->setText("Stopping once the current step finishes...");
      return true;
    });
  }
  task->dialog->open();

  s_task = task;
  s_thread = std::thread([task, work = std::move(work)] {
    Common::SetCurrentThreadName("Wii system task");
    std::function<void()> done = work(task);

    brls::sync([task, done = std::move(done)] {
      task->label = nullptr;
      if (brls::Dialog* dialog = std::exchange(task->dialog, nullptr))
      {
        dialog->close([done] {
          if (done)
            brls::sync(done);
        });
      }
    });
  });
}

std::string FailureText(const std::string& summary)
{
  const std::string alert = TakeLastAlert();
  return alert.empty() ? summary + " Check the log for details." : summary + "\n\n" + alert;
}

std::string DescribeUpdateResult(WiiUtils::UpdateResult result)
{
  using WiiUtils::UpdateResult;
  switch (result)
  {
  case UpdateResult::Succeeded:
    return "The emulated Wii has been updated.";
  case UpdateResult::AlreadyUpToDate:
    return "The emulated Wii is already up to date.";
  case UpdateResult::ServerFailed:
    return "Could not download update information. Check your internet connection and try again.";
  case UpdateResult::DownloadFailed:
    return "Could not download the update files from Nintendo's servers.\n\n"
           "Atmosphere's default DNS settings block Nintendo domains, which prevents online "
           "updates. Instead, use a Wii disc (\"Perform a system update\" in its game options) "
           "or import a BootMii NAND backup from a real Wii.";
  case UpdateResult::ImportFailed:
    return FailureText("Could not install an update to the Wii system memory.");
  case UpdateResult::Cancelled:
    return "The update was cancelled. Finishing it is strongly recommended to avoid a mix of "
           "system software versions.";
  case UpdateResult::RegionMismatch:
    return "This disc's region does not match the console's. To avoid breaking the Wii Menu, it "
           "cannot be used to update the emulated console.";
  case UpdateResult::MissingUpdatePartition:
  case UpdateResult::DiscReadFailed:
    return "This disc does not contain any usable update data.";
  default:
    return "The update failed.";
  }
}

std::function<void()>
RunUpdate(const TaskPtr& task,
          const std::function<WiiUtils::UpdateResult(const WiiUtils::UpdateCallback&)>& update,
          const NANDChangedFn& on_changed)
{
  const WiiUtils::UpdateResult result = update([&task](size_t processed, size_t total, u64) {
    SetProgressText(task, fmt::format("Updating the system software...\n\n{} of {} titles "
                                      "done.\n\nPress B to cancel.",
                                      processed, total));
    return !task->cancelled;
  });

  if (result == WiiUtils::UpdateResult::Succeeded ||
      result == WiiUtils::UpdateResult::AlreadyUpToDate)
  {
    DiscIO::NANDImporter().ExtractCertificates();
  }
  else if (result == WiiUtils::UpdateResult::DownloadFailed)
  {
    ERROR_LOG_FMT(CORE, "Online update could not reach Nintendo's CDN. If the connection was "
                        "refused instantly, Atmosphère's DNS redirection (dns.mitm) is blocking "
                        "*nintendo.net.");
  }

  return [result, on_changed] {
    NotifyChanged(on_changed);
    ShowMessage(DescribeUpdateResult(result));
  };
}

void StartOnlineUpdate(const std::string& region, NANDChangedFn on_changed)
{
  Confirm("Connect to the internet and update the emulated Wii's system software? This can take "
          "a while.",
          "Update", [region, on_changed = std::move(on_changed)] {
            StartTask("Connecting to the update server...", true,
                      [region, on_changed](const TaskPtr& task) {
                        return RunUpdate(
                            task,
                            [&region](const WiiUtils::UpdateCallback& callback) {
                              return WiiUtils::DoOnlineUpdate(callback, region);
                            },
                            on_changed);
                      });
          });
}

std::string DescribeClusters(const char* part, u64 used, u64 maximum)
{
  using namespace IOS::HLE::FS;
  return fmt::format("The {} part holds {} blocks ({} KiB), out of an allowed {} blocks ({} KiB).",
                     part, Common::AlignUp(used, CLUSTERS_PER_BLOCK) / CLUSTERS_PER_BLOCK,
                     used * CLUSTER_SIZE / 1024, maximum / CLUSTERS_PER_BLOCK,
                     maximum * CLUSTER_SIZE / 1024);
}

void StartRepairNAND(NANDChangedFn on_changed)
{
  StartTask("Repairing the Wii system memory...", false, [on_changed](const TaskPtr&) {
    IOS::HLE::Kernel ios;
    const bool repaired = WiiUtils::RepairNAND(ios);
    return std::function<void()>([repaired, on_changed] {
      NotifyChanged(on_changed);
      ShowMessage(repaired ? "The Wii system memory has been repaired." :
                             "The Wii system memory could not be repaired. Back up your data "
                             "and start again from a fresh NAND.");
    });
  });
}

void ShowNANDCheckResult(const WiiUtils::NANDCheckResult& result, NANDChangedFn on_changed)
{
  using namespace IOS::HLE::FS;

  if (!result.bad)
  {
    const bool overfull =
        result.used_clusters_user > USER_CLUSTERS || result.used_clusters_system > SYSTEM_CLUSTERS;
    ShowMessage(fmt::format(
        "{}\n\n{}\n{}",
        overfull ? "The Wii system memory holds more data than allowed. Wii software may behave "
                   "incorrectly or refuse to save." :
                   "No problems were found.",
        DescribeClusters("user", result.used_clusters_user, USER_CLUSTERS),
        DescribeClusters("system", result.used_clusters_system, SYSTEM_CLUSTERS)));
    return;
  }

  std::vector<u64> titles(result.titles_to_remove.begin(), result.titles_to_remove.end());
  std::ranges::sort(titles);

  constexpr size_t LISTED_TITLES = 6;
  std::string listed;
  for (size_t i = 0; i < std::min(titles.size(), LISTED_TITLES); ++i)
    listed += fmt::format("\n{:016x}", titles[i]);
  if (titles.size() > LISTED_TITLES)
    listed += fmt::format("\nand {} more", titles.size() - LISTED_TITLES);

  std::string text = "Problems were found in the Wii system memory.";
  if (!titles.empty())
  {
    text += fmt::format(" Repairing it removes these titles, along with their saves:\n{}\n\n"
                        "They can be reinstalled afterwards.",
                        listed);
  }
  Confirm(text, "Repair", [on_changed = std::move(on_changed)] { StartRepairNAND(on_changed); });
}

void StartNANDImport(const std::string& path, const std::string& keys_path,
                     NANDChangedFn on_changed)
{
  StartTask("Reading the NAND backup...", true, [path, keys_path, on_changed](const TaskPtr& task) {
    int last_percent = -1;
    DiscIO::NANDImporter::Step last_step = DiscIO::NANDImporter::Step::Loading;

    DiscIO::NANDImporter().ImportNANDBin(
        path,
        [&](DiscIO::NANDImporter::Step step, int current, int maximum) {
          const int percent = maximum > 0 ? current * 100 / maximum : 0;
          if (percent != last_percent || step != last_step)
          {
            last_percent = percent;
            last_step = step;
            SetProgressText(
                task,
                fmt::format("{} the NAND backup... {}%\n\nPress B to cancel.",
                            step == DiscIO::NANDImporter::Step::Loading ? "Reading" : "Extracting",
                            percent));
          }
          return task->cancelled.load();
        },
        [keys_path] { return keys_path; });

    const bool cancelled = task->cancelled;
    const std::string alert = TakeLastAlert();
    return std::function<void()>([cancelled, alert, on_changed] {
      NotifyChanged(on_changed);
      if (!alert.empty())
        ShowMessage("The NAND backup could not be imported.\n\n" + alert);
      else if (cancelled)
        ShowMessage("The import was cancelled. The Wii system memory may be incomplete.");
      else
        ShowMessage("The NAND backup has been imported.");
    });
  });
}

void ChooseNANDKeys(const std::string& path, NANDChangedFn on_changed)
{
  const u64 size = File::GetSize(path);
  if (size == NAND_BIN_SIZE + NAND_KEYS_SIZE)
  {
    StartNANDImport(path, {}, std::move(on_changed));
    return;
  }

  if (size != NAND_BIN_SIZE)
  {
    ShowMessage("This file does not look like a BootMii NAND backup.");
    return;
  }

  const std::string directory = path.substr(0, path.find_last_of('/') + 1);
  const std::string keys_path = directory + "keys.bin";
  if (File::GetSize(keys_path) == NAND_KEYS_SIZE)
  {
    StartNANDImport(path, keys_path, std::move(on_changed));
    return;
  }

  brls::Application::pushActivity(CreateFilePickerActivity(
      "Choose the keys for this NAND backup (keys.bin)", directory, {".bin"},
      [path, on_changed = std::move(on_changed)](const std::string& keys) {
        StartNANDImport(path, keys, on_changed);
      }));
}

std::string GetWiiSaveExportDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "WiiSaves" DIR_SEP;
}

std::string DescribeCopyResult(WiiSave::CopyResult result)
{
  switch (result)
  {
  case WiiSave::CopyResult::Success:
    return "The save has been imported.";
  case WiiSave::CopyResult::CorruptedSource:
    return "This save file is corrupted or is not a Wii save.";
  case WiiSave::CopyResult::TitleMissing:
    return "The game this save belongs to is not installed, so the save cannot be imported. "
           "Start the game once and try again.";
  case WiiSave::CopyResult::Cancelled:
    return "The import was cancelled.";
  case WiiSave::CopyResult::Error:
  default:
    return FailureText("The save could not be imported.");
  }
}

void StartSaveImport(const std::string& path, bool overwrite)
{
  StartTask("Importing the save...", false, [path, overwrite](const TaskPtr&) {
    bool exists = false;
    const WiiSave::CopyResult result = WiiSave::Import(path, [overwrite, &exists] {
      exists = true;
      return overwrite;
    });

    return std::function<void()>([path, result, exists, overwrite] {
      if (result == WiiSave::CopyResult::Cancelled && exists && !overwrite)
      {
        Confirm("A save for this game already exists. Replace it?", "Replace",
                [path] { StartSaveImport(path, true); });
        return;
      }
      ShowMessage(DescribeCopyResult(result));
    });
  });
}

IOS::ES::TMDReader ReadInstalledTMD(u64 title_id)
{
  std::string bytes;
  if (!File::ReadFileToString(Common::GetTMDFileName(title_id, Common::FromWhichRoot::Configured),
                              bytes))
  {
    return {};
  }
  return IOS::ES::TMDReader{std::vector<u8>(bytes.begin(), bytes.end())};
}
}  // namespace

bool IsSystemMenuInstalled()
{
  return ReadInstalledTMD(Titles::SYSTEM_MENU).IsValid();
}

std::string GetSystemMenuDescription()
{
  const IOS::ES::TMDReader tmd = ReadInstalledTMD(Titles::SYSTEM_MENU);
  if (!tmd.IsValid())
    return {};

  return fmt::format("{} {}", tmd.IsvWii() ? "vWii Menu" : "Wii Menu",
                     DiscIO::GetSysMenuVersionString(tmd.GetTitleVersion(), tmd.IsvWii()));
}

bool IsTitleInstalled(u64 title_id)
{
  const std::string content =
      Common::GetTitleContentPath(title_id, Common::FromWhichRoot::Configured);
  const File::FSTEntry entries = File::ScanDirectoryTree(content, false);
  return std::ranges::any_of(entries.children, [](const File::FSTEntry& entry) {
    return !entry.isDirectory && entry.virtualName != "title.tmd";
  });
}

void InstallWAD(const std::string& path, NANDChangedFn on_changed)
{
  StartTask("Installing to the Wii system memory...", false, [path, on_changed](const TaskPtr&) {
    const bool installed = WiiUtils::InstallWAD(path);
    const std::string message = installed ?
                                    "The title has been installed to the Wii system memory." :
                                    FailureText("The title could not be installed.");
    return std::function<void()>([message, on_changed] {
      NotifyChanged(on_changed);
      ShowMessage(message);
    });
  });
}

void ChooseAndInstallWAD(NANDChangedFn on_changed)
{
  brls::Application::pushActivity(
      CreateFilePickerActivity("Choose a WAD to install", SD_ROOT, {".wad"},
                               [on_changed = std::move(on_changed)](const std::string& path) {
                                 InstallWAD(path, on_changed);
                               }));
}

void UninstallTitle(u64 title_id, NANDChangedFn on_changed)
{
  Confirm("Remove the installed copy of this title from the Wii system memory? Its save data is "
          "kept.",
          "Uninstall", [title_id, on_changed = std::move(on_changed)] {
            StartTask("Uninstalling...", false, [title_id, on_changed](const TaskPtr&) {
              const bool removed = WiiUtils::UninstallTitle(title_id);
              const std::string message =
                  removed ? "The title has been removed from the Wii system memory." :
                            FailureText("The title could not be removed.");
              return std::function<void()>([message, on_changed] {
                NotifyChanged(on_changed);
                ShowMessage(message);
              });
            });
          });
}

void PerformOnlineUpdate(NANDChangedFn on_changed)
{
  if (IsSystemMenuInstalled())
  {
    StartOnlineUpdate({}, std::move(on_changed));
    return;
  }

  static constexpr std::array<std::pair<const char*, const char*>, 4> REGIONS = {{
      {"Europe", "EUR"},
      {"Japan", "JPN"},
      {"Korea", "KOR"},
      {"United States", "USA"},
  }};

  std::vector<std::string> labels;
  for (const auto& region : REGIONS)
    labels.push_back(region.first);

  auto choice = std::make_shared<int>(-1);
  auto* dropdown = new brls::Dropdown(
      "Console region", labels, [choice](int index) { *choice = index; }, 0,
      [choice, on_changed = std::move(on_changed)](int) {
        if (*choice < 0)
          return;
        brls::sync([region = std::string(REGIONS[*choice].second), on_changed] {
          StartOnlineUpdate(region, on_changed);
        });
      });
  brls::Application::pushActivity(new brls::Activity(dropdown));
}

void PerformDiscUpdate(const std::string& path, NANDChangedFn on_changed)
{
  Confirm("Update the emulated Wii's system software from this disc?", "Update",
          [path, on_changed = std::move(on_changed)] {
            StartTask("Reading the update from the disc...", true,
                      [path, on_changed](const TaskPtr& task) {
                        return RunUpdate(
                            task,
                            [&path](const WiiUtils::UpdateCallback& callback) {
                              return WiiUtils::DoDiscUpdate(callback, path);
                            },
                            on_changed);
                      });
          });
}

void ImportNANDBackup(NANDChangedFn on_changed)
{
  Confirm("Importing a NAND backup merges it over the current Wii system memory, replacing any "
          "channels and saves that exist in both. This cannot be undone.",
          "Continue", [on_changed = std::move(on_changed)] {
            brls::Application::pushActivity(CreateFilePickerActivity(
                "Choose a BootMii NAND backup (nand.bin)", SD_ROOT, {".bin"},
                [on_changed](const std::string& path) { ChooseNANDKeys(path, on_changed); }));
          });
}

void CheckNAND(NANDChangedFn on_changed)
{
  StartTask("Checking the Wii system memory...", false, [on_changed](const TaskPtr&) {
    IOS::HLE::Kernel ios;
    WiiUtils::NANDCheckResult result = WiiUtils::CheckNAND(ios);
    return std::function<void()>(
        [result = std::move(result), on_changed] { ShowNANDCheckResult(result, on_changed); });
  });
}

void ExtractCertificates()
{
  StartTask("Extracting certificates...", false, [](const TaskPtr&) {
    const bool extracted = DiscIO::NANDImporter().ExtractCertificates();
    const std::string message =
        extracted ? "The certificates have been extracted from the Wii system memory." :
                    FailureText("The certificates could not be extracted. Install or update the "
                                "system software first.");
    return std::function<void()>([message] { ShowMessage(message); });
  });
}

void ImportWiiSave()
{
  const std::string exports = GetWiiSaveExportDirectory();
  brls::Application::pushActivity(CreateFilePickerActivity(
      "Choose a Wii save (data.bin)", File::IsDirectory(exports) ? exports : SD_ROOT, {".bin"},
      [](const std::string& path) { StartSaveImport(path, false); }));
}

void ExportWiiSaves()
{
  StartTask("Exporting saves...", false, [](const TaskPtr&) {
    const std::string directory = GetWiiSaveExportDirectory();
    const size_t count = WiiSave::ExportAll(directory);
    return std::function<void()>([count, directory] {
      ShowMessage(count == 1 ? fmt::format("Exported 1 save to {}", directory) :
                               fmt::format("Exported {} saves to {}", count, directory));
    });
  });
}

void PackSDCard()
{
  Confirm(fmt::format("Replace everything on the SD card image with the contents of {}?",
                      File::GetUserPath(D_WIISDCARDSYNCFOLDER_IDX)),
          "Pack", [] {
            StartTask("Packing the SD card...", true, [](const TaskPtr& task) {
              const bool good =
                  Common::SyncSDFolderToSDImage([&task] { return task->cancelled.load(); }, false);
              const bool cancelled = task->cancelled;
              return std::function<void()>([good, cancelled] {
                if (!good && !cancelled)
                  ShowMessage(FailureText("Could not pack the SD card."));
                else if (good)
                  brls::Application::notify("The SD card has been packed.");
              });
            });
          });
}

void UnpackSDCard()
{
  Confirm(fmt::format("Replace everything in {} with the contents of the SD card image?",
                      File::GetUserPath(D_WIISDCARDSYNCFOLDER_IDX)),
          "Unpack", [] {
            StartTask("Unpacking the SD card...", true, [](const TaskPtr& task) {
              const bool good =
                  Common::SyncSDImageToSDFolder([&task] { return task->cancelled.load(); });
              const bool cancelled = task->cancelled;
              return std::function<void()>([good, cancelled] {
                if (!good && !cancelled)
                  ShowMessage(FailureText("Could not unpack the SD card."));
                else if (good)
                  brls::Application::notify("The SD card has been unpacked.");
              });
            });
          });
}

void StopSystemTasks()
{
  if (s_task)
    s_task->cancelled = true;
  if (s_thread.joinable())
    s_thread.join();
  s_task.reset();
}
}  // namespace Shell
