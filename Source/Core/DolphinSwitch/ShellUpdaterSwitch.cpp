// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellUpdaterSwitch.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <borealis.hpp>
#include <fmt/format.h>

#include "Common/Config/Config.h"
#include "Common/StringUtil.h"
#include "Common/Thread.h"
#include "DolphinSwitch/SettingsSwitch.h"
#include "DolphinSwitch/UpdaterSwitch.h"

namespace Shell
{
namespace
{
enum class CheckKind
{
  Silent,
  Manual,
  Notes,
};

struct InstallState
{
  brls::Label* label = nullptr;
  brls::Dialog* dialog = nullptr;
};

std::thread s_thread;
std::atomic_bool s_cancelled = false;
bool s_checking = false;
bool s_installing = false;
CheckKind s_check_kind = CheckKind::Silent;
bool s_launched = false;

void JoinWorker()
{
  if (s_thread.joinable())
    s_thread.join();
}

void ShowMessage(const std::string& text)
{
  auto* dialog = new brls::Dialog(text);
  dialog->addButton("OK", [] {});
  dialog->open();
}

std::string CleanMarkdown(std::string line)
{
  for (const std::string_view emphasis : {"**", "__", "`"})
    line = ReplaceAll(std::move(line), emphasis, "");

  for (size_t open = line.find('['); open != std::string::npos; open = line.find('[', open))
  {
    const size_t middle = line.find("](", open);
    const size_t close = middle == std::string::npos ? middle : line.find(')', middle);
    if (close == std::string::npos)
      break;
    line = line.substr(0, open) + line.substr(open + 1, middle - open - 1) + line.substr(close + 1);
  }

  if (line.starts_with("- ") || line.starts_with("* "))
    line = "• " + line.substr(2);
  return line;
}

brls::Activity* CreateNotesActivity(const std::string& tag, const std::string& notes)
{
  brls::Style style = brls::Application::getStyle();

  auto* box = new brls::Box(brls::Axis::COLUMN);
  box->setPadding(style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"],
                  style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"]);

  bool gap = false;
  for (std::string line : SplitString(notes, '\n'))
  {
    line = std::string(StripWhitespace(line));
    if (line.empty())
    {
      gap = !box->getChildren().empty();
      continue;
    }

    if (line.starts_with('#'))
    {
      const std::string_view title = StripWhitespace(
          std::string_view(line).substr(std::min(line.find_first_not_of('#'), line.size())));
      if (title.empty())
        continue;

      auto* header = new brls::Header();
      header->setTitle(CleanMarkdown(std::string(title)));
      header->setMarginTop(box->getChildren().empty() ? 0 : 24);
      box->addView(header);
      gap = false;
      continue;
    }

    auto* label = new brls::Label();
    label->setText(CleanMarkdown(std::move(line)));
    label->setFontSize(18);
    label->setMargins(gap ? 16 : 4, 16, 4, 16);
    box->addView(label);
    gap = false;
  }

  if (box->getChildren().empty())
  {
    auto* label = new brls::Label();
    label->setText("This release has no notes.");
    label->setMargins(0, 16, 0, 16);
    box->addView(label);
  }

  auto* scroll = new brls::ScrollingFrame();
  scroll->setContentView(box);

  auto* frame = new brls::AppletFrame(scroll);
  frame->setTitle(fmt::format("Nezumiiruka {}", tag));
  return new brls::Activity(frame);
}

void OpenNotes(const std::string& tag, const std::string& notes)
{
  brls::Application::pushActivity(CreateNotesActivity(tag, notes));
}

void OnInstalled(const UpdaterSwitch::Release& release)
{
  UpdaterSwitch::CacheNotes(release.tag, release.notes);
  Config::SetBase(Config::SWITCH_SKIPPED_UPDATE, std::string());

  if (!UpdaterSwitch::CanRelaunch())
  {
    ShowMessage(fmt::format("Nezumiiruka {} is installed and will be used from the next launch.",
                            release.tag));
    return;
  }

  auto* dialog = new brls::Dialog(
      fmt::format("Nezumiiruka {} is installed. Restart now?", release.tag));
  dialog->addButton("Later", [] {});
  dialog->addButton("Restart", [] {
    brls::sync([] {
      UpdaterSwitch::QueueRelaunch();
      brls::Application::quit();
    });
  });
  dialog->open();
}

void SetInstallText(const std::shared_ptr<InstallState>& state, std::string text)
{
  brls::sync([state, text = std::move(text)] {
    if (state->label)
      state->label->setText(text);
  });
}

void StartInstall(const UpdaterSwitch::Release& release)
{
  if (s_checking || s_installing)
    return;

  JoinWorker();
  s_installing = true;
  s_cancelled = false;

  auto state = std::make_shared<InstallState>();
  const std::string heading = fmt::format("Downloading Nezumiiruka {}...", release.tag);

  state->label = new brls::Label();
  state->label->setText(heading + "\n\nPress B to cancel.");
  state->label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
  state->label->setMargins(32, 32, 32, 32);

  auto* content = new brls::Box(brls::Axis::COLUMN);
  content->setFocusable(true);
  content->setHideHighlight(true);
  content->addView(state->label);

  state->dialog = new brls::Dialog(content);
  state->dialog->setCancelable(false);
  state->dialog->registerAction("Cancel", brls::BUTTON_B, [state](brls::View*) {
    if (!s_cancelled.exchange(true) && state->label)
      state->label->setText("Cancelling...");
    return true;
  });
  state->dialog->open();

  s_thread = std::thread([state, release, heading] {
    Common::SetCurrentThreadName("Nezumiiruka update");

    int last_percent = -1;
    auto result = UpdaterSwitch::Install(release, s_cancelled, [&](u64 downloaded, u64 total) {
      if (total == 0)
        total = release.size;
      const int percent = static_cast<int>(std::min<u64>(downloaded * 100 / total, 100));
      if (percent == last_percent || s_cancelled)
        return;
      last_percent = percent;
      SetInstallText(state,
                     fmt::format("{}\n\n{:.1f} of {:.1f} MiB ({}%)\n\nPress B to cancel.", heading,
                                 downloaded / 1048576.0, total / 1048576.0, percent));
    });

    brls::sync([state, release, result = std::move(result)] {
      s_installing = false;
      state->label = nullptr;
      brls::Dialog* dialog = std::exchange(state->dialog, nullptr);
      dialog->close([release, result] {
        brls::sync([release, result] {
          if (result)
            OnInstalled(release);
          else if (s_cancelled)
            brls::Application::notify("The update was cancelled.");
          else
            ShowMessage(fmt::format("Nezumiiruka could not be updated.\n\n{}", result.error()));
        });
      });
    });
  });
}

void OfferUpdate(const UpdaterSwitch::Release& release)
{
  const std::string kind = release.prerelease ? "pre-release" : "release";
  auto* dialog = new brls::Dialog(fmt::format(
      "Nezumiiruka {} is available as a {}. You have {}.",
      release.tag, kind, UpdaterSwitch::GetCurrentVersion()));
  dialog->addButton("Skip",
                    [tag = release.tag] { Config::SetBase(Config::SWITCH_SKIPPED_UPDATE, tag); });
  dialog->addButton("Later", [] {});
  dialog->addButton("Update", [release] { brls::sync([release] { StartInstall(release); }); });
  dialog->open();
}

void OnChecked(CheckKind kind, const UpdaterSwitch::CheckResult& result)
{
  const std::string current = UpdaterSwitch::GetCurrentVersion();
  if (!result.current_notes.empty())
    UpdaterSwitch::CacheNotes(current, result.current_notes);

  using UpdaterSwitch::CheckStatus;
  if (kind == CheckKind::Notes)
  {
    if (!result.current_notes.empty())
      OpenNotes(current, result.current_notes);
    else if (result.status == CheckStatus::Error)
      ShowMessage(fmt::format("Could not fetch the release notes.\n\n{}", result.error));
    else
      ShowMessage(fmt::format("GitHub lists no notes for Nezumiiruka {}.", current));
    return;
  }

  const bool manual = kind == CheckKind::Manual;
  switch (result.status)
  {
  case CheckStatus::Error:
    if (manual)
      ShowMessage(fmt::format("Could not check for updates.\n\n{}", result.error));
    break;
  case CheckStatus::UpToDate:
    if (manual)
      ShowMessage(fmt::format("Nezumiiruka {} is up to date.", current));
    break;
  case CheckStatus::Available:
    if (manual || result.release.tag != Config::Get(Config::SWITCH_SKIPPED_UPDATE))
      OfferUpdate(result.release);
    break;
  }
}

void StartCheck(CheckKind kind)
{
  if (s_installing)
  {
    if (kind != CheckKind::Silent)
      brls::Application::notify("An update is already being installed.");
    return;
  }

  if (s_checking)
  {
    if (kind == CheckKind::Manual && s_check_kind == CheckKind::Silent)
      s_check_kind = kind;
    else if (kind != CheckKind::Silent)
      brls::Application::notify("Nezumiiruka is already checking for updates.");
    return;
  }

  JoinWorker();
  s_checking = true;
  s_check_kind = kind;
  s_cancelled = false;

  const bool include_prereleases =
      Config::Get(Config::SWITCH_UPDATE_CHANNEL) == Config::UpdateChannel::Prerelease;
  s_thread = std::thread([include_prereleases] {
    Common::SetCurrentThreadName("Nezumiiruka update check");
    UpdaterSwitch::CheckResult result =
        UpdaterSwitch::CheckForUpdate(include_prereleases, s_cancelled);

    brls::sync([result = std::move(result)] {
      s_checking = false;
      OnChecked(std::exchange(s_check_kind, CheckKind::Silent), result);
    });
  });
}

void ShowUpdatedFrom(const std::string& previous)
{
  auto* dialog = new brls::Dialog(fmt::format("Nezumiiruka has been updated from {} to {}.", previous,
                                              UpdaterSwitch::GetCurrentVersion()));
  dialog->addButton("OK", [] {});
  dialog->addButton("Release notes", [] { brls::sync(ShowReleaseNotes); });
  dialog->open();
}
}  // namespace

void StartUpdaterOnLaunch()
{
  if (std::exchange(s_launched, true))
    return;

  const std::string current = UpdaterSwitch::GetCurrentVersion();
  const std::string previous = Config::Get(Config::SWITCH_LAST_RUN_VERSION);
  if (previous != current)
  {
    Config::SetBase(Config::SWITCH_LAST_RUN_VERSION, current);
    if (!previous.empty() && UpdaterSwitch::CompareVersions(current, previous) > 0)
      ShowUpdatedFrom(previous);
  }

  if (Config::Get(Config::SWITCH_CHECK_FOR_UPDATES))
    StartCheck(CheckKind::Silent);
}

void CheckForUpdates()
{
  brls::Application::notify("Checking for updates...");
  StartCheck(CheckKind::Manual);
}

void ShowReleaseNotes()
{
  const std::string current = UpdaterSwitch::GetCurrentVersion();
  const UpdaterSwitch::ReleaseNotes cached = UpdaterSwitch::LoadCachedNotes();
  if (!cached.text.empty() && UpdaterSwitch::CompareVersions(cached.tag, current) == 0)
  {
    OpenNotes(current, cached.text);
    return;
  }

  brls::Application::notify("Fetching the release notes...");
  StartCheck(CheckKind::Notes);
}

void StopUpdaterTasks()
{
  s_cancelled = true;
  JoinWorker();
  s_checking = false;
  s_installing = false;
}
}  // namespace Shell
