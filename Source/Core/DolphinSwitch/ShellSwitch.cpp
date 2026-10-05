// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellSwitch.h"

#include <switch.h>

#include <borealis.hpp>
#include <yoga/event/event.h>

#include "Common/Config/Config.h"
#include "Common/Logging/Log.h"
#ifdef USE_RETRO_ACHIEVEMENTS
#include "DolphinSwitch/ShellAchievementsSwitch.h"
#endif
#include "DolphinSwitch/ShellCheatsSwitch.h"
#include "DolphinSwitch/ShellGameListSwitch.h"
#include "DolphinSwitch/ShellLibrarySwitch.h"
#include "DolphinSwitch/ShellSystemSwitch.h"
#include "DolphinSwitch/ShellUpdaterSwitch.h"

namespace Shell
{
namespace
{
std::string s_last_chosen;

class Services
{
public:
  Services()
  {
    m_romfs = R_SUCCEEDED(romfsInit());
    plInitialize(PlServiceType_User);
    setsysInitialize();
    setInitialize();
    psmInitialize();
    nifmInitialize(NifmServiceType_User);
    lblInitialize();
  }

  ~Services()
  {
    lblExit();
    nifmExit();
    psmExit();
    setExit();
    setsysExit();
    plExit();
    if (m_romfs)
      romfsExit();
  }

  Services(const Services&) = delete;
  Services& operator=(const Services&) = delete;

private:
  bool m_romfs = false;
};

brls::View* CreateGameList(Library& library, BootRequest& chosen, const std::string& notice,
                           const std::string& focus_path)
{
  auto* frame = new brls::AppletFrame(new GameListView(library, chosen, focus_path));
  frame->setTitle("ネズミイルカ");

  if (!notice.empty())
  {
    brls::sync([notice] {
      auto* dialog = new brls::Dialog(notice);
      dialog->addButton("OK", [] {});
      dialog->open();
    });
  }

  return frame;
}

void RestoreAppletState()
{
  if (appletGetAppletType() != AppletType_Application)
    return;

  appletSetFocusHandlingMode(AppletFocusHandlingMode_SuspendHomeSleep);
  appletSetWirelessPriorityMode(AppletWirelessPriorityMode_Default);
}
}  // namespace

BootRequest Run(const std::string& notice)
{
  Services services;

  if (!brls::Application::init())
  {
    ERROR_LOG_FMT(COMMON, "Could not start the shell.");
    return {};
  }

  brls::Application::createWindow("nezumiruka");
  brls::Application::setGlobalQuit(true);

  BootRequest chosen;
  {
    Library library;
    brls::Application::pushActivity(
        new brls::Activity(CreateGameList(library, chosen, notice, s_last_chosen)));
    brls::sync(StartUpdaterOnLaunch);

    while (brls::Application::mainLoop())
    {
    }

    library.Stop();
  }

  StopSystemTasks();
  StopCheatDownloads();
  StopUpdaterTasks();
#ifdef USE_RETRO_ACHIEVEMENTS
  StopAchievementFetches();
#endif

  brls::Threading::getSyncFunctions()->clear();

  facebook::yoga::Event::reset();
  RestoreAppletState();

  Config::Save();

  if (!chosen.path.empty())
    s_last_chosen = chosen.path;
  return chosen;
}
}  // namespace Shell
