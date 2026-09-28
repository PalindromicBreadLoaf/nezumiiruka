// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellSwitch.h"

#include <algorithm>
#include <string_view>
#include <vector>

#include <switch.h>

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <yoga/event/event.h>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/StringUtil.h"
#include "Common/Version.h"
#include "UICommon/GameFileCache.h"

namespace Shell
{
namespace
{
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

std::vector<std::string> ScanGames()
{
  const std::string directory = GetRomDirectory();
  const std::string_view directory_view = directory;
  std::vector<std::string> paths = UICommon::FindAllGamePaths({&directory_view, 1}, true);
  std::ranges::sort(paths);
  return paths;
}

void AddMessage(brls::Box* list, const std::string& text)
{
  auto* label = new brls::Label();
  label->setText(text);
  label->setMargins(12, 40, 12, 40);
  list->addView(label);
}

void Populate(brls::Box* list, std::string& chosen, const std::string& notice)
{
  list->clearViews();

  if (!notice.empty())
    AddMessage(list, notice);

  const std::vector<std::string> games = ScanGames();
  if (games.empty())
  {
    AddMessage(list, "Nothing here. Copy GameCube or Wii images into " + GetRomDirectory());
    return;
  }

  // TODO: Move to a RecyclerFrame once the list carries cover art.
  brls::View* first = nullptr;
  for (const std::string& path : games)
  {
    std::string name, extension;
    SplitPath(path, nullptr, &name, &extension);
    if (!extension.empty())
      extension.erase(0, 1);
    Common::ToUpper(&extension);

    auto* cell = new brls::DetailCell();
    cell->setText(name);
    cell->setDetailText(extension);
    cell->registerClickAction([&chosen, path](brls::View*) {
      chosen = path;
      brls::Application::quit();
      return true;
    });
    list->addView(cell);

    if (!first)
      first = cell;
  }

  brls::Application::giveFocus(first);
}

brls::View* CreateGameList(std::string& chosen, const std::string& notice)
{
  auto* list = new brls::Box(brls::Axis::COLUMN);
  auto* scroll = new brls::ScrollingFrame();
  scroll->setContentView(list);

  auto* frame = new brls::AppletFrame(scroll);
  frame->setTitle("porpoise " + Common::GetScmDescStr());
  frame->registerAction("Rescan", brls::BUTTON_X, [list, &chosen, notice](brls::View*) {
    Populate(list, chosen, notice);
    return true;
  });

  Populate(list, chosen, notice);
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

std::string GetRomDirectory()
{
  return File::GetUserPath(D_USER_IDX) + "roms" DIR_SEP;
}

std::string Run(const std::string& notice)
{
  Services services;

  if (!brls::Application::init())
  {
    ERROR_LOG_FMT(COMMON, "Could not start the shell.");
    return {};
  }

  brls::Application::createWindow("porpoise");
  brls::Application::setGlobalQuit(true);

  std::string chosen;
  brls::Application::pushActivity(new brls::Activity(CreateGameList(chosen, notice)));

  while (brls::Application::mainLoop())
  {
  }

  facebook::yoga::Event::reset();
  RestoreAppletState();

  return chosen;
}
}  // namespace Shell
