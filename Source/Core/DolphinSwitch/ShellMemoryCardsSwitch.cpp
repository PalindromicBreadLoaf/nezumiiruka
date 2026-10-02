// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellMemoryCardsSwitch.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <borealis.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/dropdown.hpp>
#include <fmt/format.h>
#include <nanovg.h>

#include "Common/FileUtil.h"
#include "Core/Config/MainSettings.h"
#include "Core/HW/GCMemcard/GCMemcardUtils.h"
#include "DolphinSwitch/MemoryCardsSwitch.h"
#include "DolphinSwitch/ShellFilePickerSwitch.h"
#include "DolphinSwitch/ShellFormatSwitch.h"
#include "UICommon/GameFile.h"

namespace Shell
{
namespace
{
using MemoryCards::Card;
using MemoryCards::ImportResult;
using MemoryCards::Save;
using MemoryCards::Slot;

constexpr const char* SD_ROOT = "sdmc:/";
constexpr float ICON_SIZE = 40;

struct ManagerState
{
  DiscIO::Region region;
  std::string game_code;
};

using StatePtr = std::shared_ptr<ManagerState>;

NVGcolor ThemeColour(const std::string& name)
{
  brls::Theme theme = brls::Application::getTheme();
  return theme[name];
}

std::string FormatBlocks(size_t blocks)
{
  return blocks == 1 ? std::string("1 block") : fmt::format("{} blocks", blocks);
}

std::string FormatSaves(size_t saves)
{
  return saves == 1 ? std::string("1 save") : fmt::format("{} saves", saves);
}

std::string GetSaveKey(const Save& save)
{
  return "save:" + Memcard::GenerateFilename(save.file.dir_entry);
}

void ShowDropdown(const std::string& title, const std::vector<std::string>& labels,
                  std::function<void(int)> chosen)
{
  auto choice = std::make_shared<int>(-1);
  auto* dropdown = new brls::Dropdown(
      title, labels, [choice](int index) { *choice = index; }, 0,
      [choice, chosen = std::move(chosen)](int) {
        if (*choice >= 0)
          chosen(*choice);
      });
  brls::Application::pushActivity(new brls::Activity(dropdown));
}

void Confirm(const std::string& text, const std::string& action, std::function<void()> confirmed)
{
  auto* dialog = new brls::Dialog(text);
  dialog->addButton("Cancel", [] {});
  dialog->addButton(action, std::move(confirmed));
  dialog->open();
}

void Report(ImportResult result, const std::string& success)
{
  switch (result)
  {
  case ImportResult::Success:
    brls::Application::notify(success);
    break;
  case ImportResult::OutOfBlocks:
    brls::Application::notify("There is not enough free space on the card.");
    break;
  case ImportResult::OutOfEntries:
    brls::Application::notify("The card cannot hold any more saves.");
    break;
  case ImportResult::AlreadyPresent:
  case ImportResult::Failed:
    brls::Application::notify("The save could not be written.");
    break;
  }
}

class SaveCell final : public brls::DetailCell
{
public:
  explicit SaveCell(const Save& save)
  {
    setText(save.title);
    setDetailText(FormatBlocks(save.file.blocks.size()));

    auto* icon = new brls::Image();
    icon->setDimensions(ICON_SIZE, ICON_SIZE);
    icon->setMarginRight(16);
    icon->setScalingType(brls::ImageScalingType::FIT);
    icon->setInterpolation(brls::ImageInterpolation::NEAREST);
    addView(icon, 0);

    const auto pixels = MemoryCards::DecodeIcon(save.file);
    if (!pixels)
      return;

    std::vector<u8> rgba(pixels->size() * 4);
    for (size_t i = 0; i < pixels->size(); ++i)
    {
      const u32 pixel = (*pixels)[i];
      rgba[i * 4 + 0] = static_cast<u8>(pixel >> 16);
      rgba[i * 4 + 1] = static_cast<u8>(pixel >> 8);
      rgba[i * 4 + 2] = static_cast<u8>(pixel);
      rgba[i * 4 + 3] = static_cast<u8>(pixel >> 24);
    }

    const int texture =
        nvgCreateImageRGBA(brls::Application::getNVGContext(), Memcard::MEMORY_CARD_ICON_WIDTH,
                           Memcard::MEMORY_CARD_ICON_HEIGHT, NVG_IMAGE_NEAREST, rgba.data());
    if (texture != 0)
      icon->innerSetImage(texture);
  }
};

class CardView final : public brls::ScrollingFrame
{
public:
  CardView(StatePtr state, Slot slot) : m_state(std::move(state)), m_slot(slot)
  {
    brls::Style style = brls::Application::getStyle();
    m_list = new brls::Box(brls::Axis::COLUMN);
    m_list->setPadding(style["brls/tab_frame/content_padding_top_bottom"],
                       style["brls/tab_frame/content_padding_sides"],
                       style["brls/tab_frame/content_padding_top_bottom"],
                       style["brls/tab_frame/content_padding_sides"]);
    setContentView(m_list);

    m_subscription = brls::Application::getRunLoopEvent()->subscribe([this] {
      if (!m_rebuild_pending)
        return;

      const std::vector<brls::Activity*> stack = brls::Application::getActivitiesStack();
      if (!stack.empty() && stack.back() == getParentActivity())
        Rebuild(true);
    });

    Rebuild(false);
  }

  ~CardView() override { brls::Application::getRunLoopEvent()->unsubscribe(m_subscription); }

private:
  bool IsFiltered() const { return !m_state->game_code.empty(); }

  const char* GetSlotLabel() const { return MemoryCards::GetSlotLabel(m_slot); }

  void AddHeader(const std::string& title, const std::string& subtitle = {})
  {
    auto* header = new brls::Header();
    header->setTitle(title);
    if (!subtitle.empty())
      header->setSubtitle(subtitle);
    header->setMarginTop(m_list->getChildren().empty() ? 0 : 24);
    m_list->addView(header);
  }

  void AddNote(const std::string& text)
  {
    auto* label = new brls::Label();
    label->setText(text);
    label->setFontSize(16);
    label->setTextColor(ThemeColour("brls/header/subtitle"));
    label->setVerticalAlign(brls::VerticalAlign::TOP);
    label->setMargins(8, 16, 16, 16);
    m_list->addView(label);
  }

  void AddInfo(const std::string& title, const std::string& value)
  {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(value);
    cell->setDetailTextColor(ThemeColour("brls/text"));
    m_list->addView(cell);
    Track(cell, {});
  }

  void AddAction(const std::string& title, const std::string& key, std::function<void()> action)
  {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->registerClickAction([action = std::move(action)](brls::View*) {
      action();
      return true;
    });
    m_list->addView(cell);
    Track(cell, key);
  }

  void Track(brls::View* view, const std::string& key)
  {
    if (!m_first_focus)
      m_first_focus = view;
    if (!key.empty() && key == m_focus_key)
      m_focus = view;
  }

  void ScheduleRebuild(std::string focus_key)
  {
    m_focus_key = std::move(focus_key);
    m_rebuild_pending = true;
  }

  std::vector<const Save*> GetShownSaves() const
  {
    std::vector<const Save*> saves;
    for (const Save& save : m_card->GetSaves())
    {
      if (!IsFiltered() || MemoryCards::GetGameCode(save.file.dir_entry) == m_state->game_code)
        saves.push_back(&save);
    }
    return saves;
  }

  void AddRegion()
  {
    if (IsFiltered())
    {
      AddInfo("Region", MemoryCards::GetRegionLabel(m_state->region));
      return;
    }

    std::vector<std::string> labels;
    int selected = 0;
    for (size_t i = 0; i < MemoryCards::REGIONS.size(); ++i)
    {
      labels.push_back(MemoryCards::GetRegionLabel(MemoryCards::REGIONS[i]));
      if (MemoryCards::REGIONS[i] == m_state->region)
        selected = static_cast<int>(i);
    }

    auto* cell = new brls::SelectorCell();
    cell->init("Region", labels, selected, [this](int index) {
      m_state->region = MemoryCards::REGIONS[index];
      ScheduleRebuild("region");
    });
    m_list->addView(cell);
    Track(cell, "region");
  }

  void Rebuild(bool give_focus)
  {
    m_rebuild_pending = false;
    m_first_focus = nullptr;
    m_focus = nullptr;
    m_list->clearViews();

    m_card = std::make_unique<Card>(m_slot, m_state->region);

    AddHeader("Card");
    AddRegion();

    if (!m_card->IsInserted())
    {
      AddNote(fmt::format("No memory card is inserted in {}. Choose one on the GameCube settings "
                          "page.",
                          GetSlotLabel()));
      Finish(give_focus);
      return;
    }

    const std::vector<const Save*> saves = GetShownSaves();

    AddInfo("Type", m_card->IsFolder() ? "GCI folder" : "Raw file");
    if (m_card->IsReadable())
    {
      AddInfo("Free space", fmt::format("{} of {}", FormatBlocks(m_card->GetFreeBlocks()),
                                        FormatBlocks(m_card->GetTotalBlocks())));
    }
    AddInfo("Location", m_card->GetPath());

    if (!m_card->IsReadable())
    {
      AddNote("This card could not be read.");
      AddAction("Format the card", "erase", [this] { Erase(); });
      Finish(give_focus);
      return;
    }

    AddHeader("Manage");
    AddAction("Import a save", "import", [this] { ChooseImport(); });
    if (!saves.empty())
    {
      AddAction(IsFiltered() ? "Export these saves" : "Export every save", "export",
                [this] { ExportAll(); });
    }
    if (!IsFiltered())
    {
      AddAction(m_card->IsFolder() ? "Delete every save" : "Format the card", "erase",
                [this] { Erase(); });
    }
    AddNote(fmt::format("Saves are exported to {} as GCI files. GCS and SAV files can be imported "
                        "as well.",
                        MemoryCards::GetExportDirectory()));

    AddHeader("Saves", FormatSaves(saves.size()));
    if (saves.empty())
      AddNote(IsFiltered() ? "This game has no saves on this card." : "This card is empty.");

    for (size_t i = 0; i < saves.size(); ++i)
    {
      const Save& save = *saves[i];
      auto* cell = new SaveCell(save);

      const std::string key = GetSaveKey(save);
      const std::string neighbour =
          saves.size() == 1 ? "import" : GetSaveKey(*saves[i + 1 < saves.size() ? i + 1 : i - 1]);
      cell->registerClickAction([this, entry = save.file.dir_entry, neighbour](brls::View*) {
        ShowSaveActions(entry, neighbour);
        return true;
      });
      m_list->addView(cell);
      Track(cell, key);
    }

    Finish(give_focus);
  }

  void Finish(bool give_focus)
  {
    if (give_focus)
      brls::Application::giveFocus(m_focus ? m_focus : m_first_focus);
  }

  const Save* Find(const Memcard::DEntry& entry) const
  {
    const std::optional<size_t> index = m_card->Find(entry);
    return index ? &m_card->GetSaves()[*index] : nullptr;
  }

  void ShowSaveActions(const Memcard::DEntry& entry, const std::string& neighbour)
  {
    const Save* save = Find(entry);
    if (!save)
      return;

    std::vector<std::string> labels;
    std::vector<std::function<void()>> actions;

    const Slot other = MemoryCards::GetOtherSlot(m_slot);
    if (MemoryCards::IsInserted(other))
    {
      labels.push_back(fmt::format("Copy to {}", MemoryCards::GetSlotLabel(other)));
      actions.push_back([this, entry] { Copy(entry); });
    }
    labels.push_back("Export");
    actions.push_back([this, entry] { Export(entry); });
    labels.push_back("Delete");
    actions.push_back([this, entry, neighbour] { Delete(entry, neighbour); });

    const std::string title =
        save->comment.empty() ? save->title : fmt::format("{} – {}", save->title, save->comment);
    ShowDropdown(title, labels, [actions](int index) { brls::sync(actions[index]); });
  }

  void Copy(const Memcard::DEntry& entry)
  {
    const Save* save = Find(entry);
    if (!save)
      return;

    const Slot slot = MemoryCards::GetOtherSlot(m_slot);
    const std::string done = fmt::format("Copied to {}.", MemoryCards::GetSlotLabel(slot));
    auto target = std::make_shared<Card>(slot, m_state->region);
    const ImportResult result = target->Import(save->file, false);
    if (result != ImportResult::AlreadyPresent)
    {
      Report(result, done);
      return;
    }

    Confirm(fmt::format("{} already has this save. Replace it?", MemoryCards::GetSlotLabel(slot)),
            "Replace",
            [target, file = save->file, done] { Report(target->Import(file, true), done); });
  }

  void Export(const Memcard::DEntry& entry)
  {
    const Save* save = Find(entry);
    if (!save)
      return;

    const std::optional<std::string> path = MemoryCards::Export(*save);
    brls::Application::notify(path ? fmt::format("Exported to {}", *path) :
                                     std::string("The save could not be exported."));
  }

  void ExportAll()
  {
    size_t exported = 0;
    for (const Save* save : GetShownSaves())
      exported += MemoryCards::Export(*save).has_value();

    brls::Application::notify(
        fmt::format("Exported {} to {}", FormatSaves(exported), MemoryCards::GetExportDirectory()));
  }

  void Delete(const Memcard::DEntry& entry, const std::string& neighbour)
  {
    const Save* save = Find(entry);
    if (!save)
      return;

    Confirm(
        fmt::format("Delete \"{}\" from {}? This cannot be undone.", save->title, GetSlotLabel()),
        "Delete", [this, entry, neighbour] {
          const std::optional<size_t> index = m_card->Find(entry);
          if (!index || !m_card->Remove(*index))
            brls::Application::notify("The save could not be deleted.");
          ScheduleRebuild(neighbour);
        });
  }

  void ChooseImport()
  {
    const std::string exports = MemoryCards::GetExportDirectory();
    brls::Application::pushActivity(CreateFilePickerActivity(
        "Choose a GameCube save", File::IsDirectory(exports) ? exports : SD_ROOT,
        {".gci", ".gcs", ".sav"}, [this](const std::string& path) { Import(path); }));
  }

  void Import(const std::string& path)
  {
    auto read = Memcard::ReadSavefile(path);
    auto* savefile = std::get_if<Memcard::Savefile>(&read);
    if (!savefile)
    {
      brls::Application::notify("The file is either not a GameCube save or is damaged.");
      return;
    }

    const std::string key = "save:" + Memcard::GenerateFilename(savefile->dir_entry);
    const std::string done = fmt::format("Imported to {}.", GetSlotLabel());
    const ImportResult result = m_card->Import(*savefile, false);
    if (result != ImportResult::AlreadyPresent)
    {
      Report(result, done);
      ScheduleRebuild(key);
      return;
    }

    Confirm("This card already has this save. Replace it?", "Replace",
            [this, file = std::move(*savefile), key, done] {
              Report(m_card->Import(file, true), done);
              ScheduleRebuild(key);
            });
  }

  void Erase()
  {
    const std::string question =
        m_card->IsFolder() ?
            fmt::format("Delete every save in {}? This cannot be undone.", GetSlotLabel()) :
            fmt::format("Format the card in {}? Everything on it will be erased.", GetSlotLabel());

    Confirm(question, m_card->IsFolder() ? "Delete" : "Format", [this] {
      brls::Application::notify(m_card->Erase() ? std::string("The card is now empty.") :
                                                  std::string("The card could not be erased."));
      ScheduleRebuild("erase");
    });
  }

  StatePtr m_state;
  Slot m_slot;
  std::unique_ptr<Card> m_card;

  brls::Box* m_list = nullptr;
  brls::View* m_first_focus = nullptr;
  brls::View* m_focus = nullptr;
  std::string m_focus_key;
  bool m_rebuild_pending = false;
  brls::VoidEvent::Subscription m_subscription;
};

brls::Activity* CreateActivity(StatePtr state, const std::string& title)
{
  auto* tabs = new brls::TabFrame();
  for (const Slot slot : {Slot::A, Slot::B})
    tabs->addTab(MemoryCards::GetSlotLabel(slot),
                 [state, slot] { return new CardView(state, slot); });

  auto* frame = new brls::AppletFrame(tabs);
  frame->setTitle(title);
  return new brls::Activity(frame);
}
}  // namespace

brls::Activity* CreateMemoryCardsActivity()
{
  return CreateActivity(std::make_shared<ManagerState>(MemoryCards::GetDefaultRegion(), ""),
                        "Memory cards");
}

brls::Activity* CreateMemoryCardsActivity(const UICommon::GameFile& game)
{
  return CreateActivity(std::make_shared<ManagerState>(Config::ToGameCubeRegion(game.GetRegion()),
                                                       game.GetGameID().substr(0, 4)),
                        fmt::format("Saves – {}", GetTitle(game)));
}
}  // namespace Shell
