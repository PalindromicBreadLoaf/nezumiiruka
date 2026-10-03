// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellFilePickerSwitch.h"

#include <algorithm>
#include <utility>

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>

#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/StringUtil.h"
#include "DolphinSwitch/UsbStorageSwitch.h"
#include "UICommon/UICommon.h"

namespace Shell
{
namespace
{
constexpr const char* SD_ROOT = "sdmc:/";

std::string WithTrailingSeparator(std::string directory)
{
  if (!directory.empty() && directory.back() != '/')
    directory.push_back('/');
  return directory;
}

std::string GetParent(const std::string& directory)
{
  std::string trimmed = directory;
  if (trimmed.size() > 1 && trimmed.back() == '/')
    trimmed.pop_back();

  const size_t slash = trimmed.find_last_of('/');
  if (slash == std::string::npos)
    return {};

  std::string parent = trimmed.substr(0, slash + 1);
  return parent == directory ? std::string{} : parent;
}

std::string ToLower(std::string text)
{
  Common::ToLower(&text);
  return text;
}

class FilePickerView : public brls::Box
{
public:
  FilePickerView(const std::string& directory, std::vector<std::string> extensions,
                 FileChosenFn on_chosen)
      : brls::Box(brls::Axis::COLUMN), m_extensions(std::move(extensions)),
        m_on_chosen(std::move(on_chosen))
  {
    setGrow(1);

    for (std::string& extension : m_extensions)
      extension = ToLower(extension);

    m_path = new brls::Label();
    m_path->setFontSize(16);
    m_path->setSingleLine(true);
    m_path->setTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    m_path->setMargins(16, 40, 8, 40);
    addView(m_path);

    m_list = new brls::Box(brls::Axis::COLUMN);
    m_list->setPadding(8, 40, 32, 40);
    m_scroll = new brls::ScrollingFrame();
    m_scroll->setGrow(1);
    m_scroll->setContentView(m_list);
    addView(m_scroll);

    std::string start = WithTrailingSeparator(directory);
    if (!File::IsDirectory(start))
      start = File::GetUserPath(D_USER_IDX);
    Populate(start, {}, false);
  }

private:
  bool Matches(const std::string& name) const
  {
    const std::string lower = ToLower(name);
    return std::ranges::any_of(m_extensions, [&lower](const std::string& extension) {
      return lower.ends_with(extension);
    });
  }

  brls::DetailCell* AddCell(const std::string& text, const std::string& detail,
                            std::function<void()> action)
  {
    auto* cell = new brls::DetailCell();
    cell->setText(text);
    cell->setDetailText(detail);
    cell->setDetailTextColor(brls::Application::getTheme()["brls/text_disabled"]);
    cell->registerClickAction([action = std::move(action)](brls::View*) {
      brls::sync(action);
      return true;
    });
    m_list->addView(cell);
    return cell;
  }

  void Populate(const std::string& directory, const std::string& focus_name, bool give_focus = true)
  {
    m_list->clearViews();
    m_path->setText(directory);

    std::vector<File::FSTEntry> folders;
    std::vector<File::FSTEntry> files;
    for (File::FSTEntry& entry : File::ScanDirectoryTree(directory, false).children)
    {
      if (entry.virtualName.starts_with('.'))
        continue;
      if (entry.isDirectory)
        folders.push_back(std::move(entry));
      else if (Matches(entry.virtualName))
        files.push_back(std::move(entry));
    }

    const auto by_name = [](const File::FSTEntry& a, const File::FSTEntry& b) {
      return ToLower(a.virtualName) < ToLower(b.virtualName);
    };
    std::ranges::sort(folders, by_name);
    std::ranges::sort(files, by_name);

    brls::View* focus = nullptr;
    brls::View* first_drive = nullptr;

    const std::string parent_directory = GetParent(directory);
    if (!parent_directory.empty())
    {
      std::string name = directory.substr(parent_directory.size());
      if (!name.empty() && name.back() == '/')
        name.pop_back();
      focus = AddCell("..", "Parent folder",
                      [this, parent_directory, name] { Populate(parent_directory, name); });
    }
    else
    {
      std::vector<UsbStorage::Volume> drives = UsbStorage::GetVolumes();
      drives.insert(drives.begin(), {.root = SD_ROOT, .label = "SD card"});
      for (const UsbStorage::Volume& drive : drives)
      {
        if (drive.root == directory)
          continue;
        brls::View* cell =
            AddCell(drive.label, drive.root, [this, root = drive.root] { Populate(root, {}); });
        if (first_drive == nullptr)
          first_drive = cell;
      }
    }

    for (const File::FSTEntry& folder : folders)
    {
      const std::string path = WithTrailingSeparator(directory + folder.virtualName);
      brls::View* cell =
          AddCell(folder.virtualName + "/", "Folder", [this, path] { Populate(path, {}); });
      if (focus == nullptr || folder.virtualName == focus_name)
        focus = cell;
    }

    for (const File::FSTEntry& file : files)
    {
      brls::View* cell = AddCell(file.virtualName, UICommon::FormatSize(file.size),
                                 [this, path = directory + file.virtualName] { Choose(path); });
      if (focus == nullptr)
        focus = cell;
    }

    if (focus == nullptr)
      focus = first_drive;

    if (focus == nullptr)
    {
      auto* empty = new brls::DetailCell();
      empty->setText("Nothing to choose here");
      m_list->addView(empty);
      focus = empty;
    }

    m_scroll->setContentOffsetY(0, false);
    if (give_focus)
      brls::Application::giveFocus(focus);
  }

  void Choose(const std::string& path)
  {
    brls::Application::popActivity(
        brls::TransitionAnimation::FADE,
        [on_chosen = m_on_chosen, path] { brls::sync([on_chosen, path] { on_chosen(path); }); });
  }

  std::vector<std::string> m_extensions;
  FileChosenFn m_on_chosen;

  brls::Label* m_path = nullptr;
  brls::ScrollingFrame* m_scroll = nullptr;
  brls::Box* m_list = nullptr;
};
}  // namespace

brls::Activity* CreateFilePickerActivity(const std::string& title, const std::string& directory,
                                         std::vector<std::string> extensions,
                                         FileChosenFn on_chosen)
{
  auto* frame = new brls::AppletFrame(
      new FilePickerView(directory, std::move(extensions), std::move(on_chosen)));
  frame->setTitle(title);
  return new brls::Activity(frame);
}
}  // namespace Shell
