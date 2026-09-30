// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "DolphinSwitch/ShellControlsSwitch.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include <switch.h>

#include <borealis.hpp>
#include <borealis/views/cells/cell_bool.hpp>
#include <borealis/views/cells/cell_detail.hpp>
#include <borealis/views/cells/cell_selector.hpp>
#include <borealis/views/cells/cell_slider.hpp>
#include <fmt/format.h>

#include "Common/StringUtil.h"
#include "Core/HW/GCPadEmu.h"
#include "InputCommon/ControlReference/ControlReference.h"
#include "InputCommon/ControllerEmu/Control/Control.h"
#include "InputCommon/ControllerEmu/ControlGroup/Attachments.h"
#include "InputCommon/ControllerEmu/ControlGroup/ControlGroup.h"
#include "InputCommon/ControllerEmu/ControllerEmu.h"
#include "InputCommon/ControllerEmu/Setting/NumericSetting.h"
#include "InputCommon/ControllerInterface/Horizon/Horizon.h"

namespace Shell
{
namespace
{
using ControllerEmu::ControlGroup;
using ControllerEmu::GroupType;
using ControllerProfiles::Kind;

constexpr auto CAPTURE_TIME = std::chrono::seconds(5);

struct InputChoice
{
  const char* label;
  const char* expression;
};

constexpr std::array INPUT_CHOICES{
    InputChoice{"A", "`A`"},
    InputChoice{"B", "`B`"},
    InputChoice{"X", "`X`"},
    InputChoice{"Y", "`Y`"},
    InputChoice{"L", "`L`"},
    InputChoice{"R", "`R`"},
    InputChoice{"ZL", "`ZL`"},
    InputChoice{"ZR", "`ZR`"},
    InputChoice{"Plus", "`Plus`"},
    InputChoice{"Minus", "`Minus`"},
    InputChoice{"Left stick press", "`Stick L`"},
    InputChoice{"Right stick press", "`Stick R`"},
    InputChoice{"D-pad up", "`Pad Up`"},
    InputChoice{"D-pad down", "`Pad Down`"},
    InputChoice{"D-pad left", "`Pad Left`"},
    InputChoice{"D-pad right", "`Pad Right`"},
    InputChoice{"Left stick up", "`Left Y+`"},
    InputChoice{"Left stick down", "`Left Y-`"},
    InputChoice{"Left stick left", "`Left X-`"},
    InputChoice{"Left stick right", "`Left X+`"},
    InputChoice{"Right stick up", "`Right Y+`"},
    InputChoice{"Right stick down", "`Right Y-`"},
    InputChoice{"Right stick left", "`Right X-`"},
    InputChoice{"Right stick right", "`Right X+`"},
    InputChoice{"SL", "`SL`"},
    InputChoice{"SR", "`SR`"},
    InputChoice{"GameCube L, analogue", "`Trigger L`"},
    InputChoice{"GameCube R, analogue", "`Trigger R`"},
    InputChoice{"Sideways Joy-Con, right button", "`Sideways Right`"},
    InputChoice{"Sideways Joy-Con, bottom button", "`Sideways Down`"},
    InputChoice{"Sideways Joy-Con, left button", "`Sideways Left`"},
    InputChoice{"Sideways Joy-Con, top button", "`Sideways Up`"},
    InputChoice{"Sideways Joy-Con, Plus or Minus", "`Sideways Menu`"},
    InputChoice{"Sideways Joy-Con, stick press", "`Sideways Stick Click`"},
    InputChoice{"Sideways Joy-Con, stick up", "`Sideways Stick Y+`"},
    InputChoice{"Sideways Joy-Con, stick down", "`Sideways Stick Y-`"},
    InputChoice{"Sideways Joy-Con, stick left", "`Sideways Stick X-`"},
    InputChoice{"Sideways Joy-Con, stick right", "`Sideways Stick X+`"},
    InputChoice{"Touch screen press", "`Horizon/0/Touchscreen:Touch`"},
};

constexpr std::array OUTPUT_CHOICES{
    InputChoice{"Both motors", "`Motor`"},
    InputChoice{"Left motor", "`Motor L`"},
    InputChoice{"Right motor", "`Motor R`"},
};

using Directions = std::array<const char*, 4>;

constexpr Directions LEFT_STICK{"`Left Y+`", "`Left Y-`", "`Left X-`", "`Left X+`"};
constexpr Directions RIGHT_STICK{"`Right Y+`", "`Right Y-`", "`Right X-`", "`Right X+`"};
constexpr Directions DPAD{"`Pad Up`", "`Pad Down`", "`Pad Left`", "`Pad Right`"};
constexpr Directions DPAD_OR_LEFT_STICK{"`Pad Up` | `Left Y+`", "`Pad Down` | `Left Y-`",
                                        "`Pad Left` | `Left X-`", "`Pad Right` | `Left X+`"};
constexpr Directions TOUCH_SCREEN{
    "`Horizon/0/Touchscreen:Touch Y-`", "`Horizon/0/Touchscreen:Touch Y+`",
    "`Horizon/0/Touchscreen:Touch X-`", "`Horizon/0/Touchscreen:Touch X+`"};
constexpr Directions NOTHING{"", "", "", ""};

struct Source
{
  const char* label;
  Directions expressions;
};

const std::vector<Source> STICK_SOURCES{
    {"Left stick", LEFT_STICK},
    {"Right stick", RIGHT_STICK},
    {"D-pad", DPAD},
    {"Nothing", NOTHING},
};

const std::vector<Source> DPAD_SOURCES{
    {"D-pad", DPAD},
    {"Left stick", LEFT_STICK},
    {"Right stick", RIGHT_STICK},
    {"D-pad or left stick", DPAD_OR_LEFT_STICK},
    {"Nothing", NOTHING},
};

const std::vector<Source> POINTER_SOURCES{
    {"Touch screen", TOUCH_SCREEN},
    {"Left stick", LEFT_STICK},
    {"Right stick", RIGHT_STICK},
    {"Nothing", NOTHING},
};

struct MotionSource
{
  const char* label;
  const char* prefix;
};

constexpr std::array MOTION_SOURCES{
    MotionSource{"Right Joy-Con or whole controller", ""},
    MotionSource{"Left Joy-Con", "Left "},
    MotionSource{"Sideways Joy-Con", "Sideways "},
    MotionSource{"Nothing", nullptr},
};

constexpr std::array ACCEL_INPUTS{"Accel Up",    "Accel Down",    "Accel Left",
                                  "Accel Right", "Accel Forward", "Accel Backward"};
constexpr std::array GYRO_INPUTS{"Gyro Pitch Up",   "Gyro Pitch Down", "Gyro Roll Left",
                                 "Gyro Roll Right", "Gyro Yaw Left",   "Gyro Yaw Right"};

NVGcolor ThemeColour(const std::string& name)
{
  return brls::Application::getTheme()[name];
}

void AddHeader(brls::Box* box, const std::string& title)
{
  auto* header = new brls::Header();
  header->setTitle(title);
  header->setMarginTop(box->getChildren().empty() ? 0 : 24);
  box->addView(header);
}

void AddNote(brls::Box* box, const std::string& text)
{
  auto* label = new brls::Label();
  label->setText(text);
  label->setFontSize(16);
  label->setTextColor(ThemeColour("brls/header/subtitle"));
  label->setVerticalAlign(brls::VerticalAlign::TOP);
  label->setMargins(8, 16, 16, 16);
  box->addView(label);
}

brls::Box* CreateListBox()
{
  brls::Style style = brls::Application::getStyle();

  auto* box = new brls::Box(brls::Axis::COLUMN);
  box->setPadding(style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"],
                  style["brls/tab_frame/content_padding_top_bottom"],
                  style["brls/tab_frame/content_padding_sides"]);
  return box;
}

void ShowDropdown(const std::string& title, const std::vector<std::string>& labels, int selected,
                  std::function<void(int)> chosen)
{
  auto choice = std::make_shared<int>(-1);
  auto* dropdown = new brls::Dropdown(
      title, labels, [choice](int index) { *choice = index; }, std::max(selected, 0),
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

std::optional<std::string> GetChoiceLabel(std::string_view expression)
{
  for (const InputChoice& choice : INPUT_CHOICES)
  {
    if (expression == choice.expression)
      return choice.label;
  }
  for (const InputChoice& choice : OUTPUT_CHOICES)
  {
    if (expression == choice.expression)
      return choice.label;
  }
  return std::nullopt;
}

std::string DescribeExpression(const std::string& expression)
{
  if (expression.empty())
    return "Unbound";

  std::string description;
  for (const std::string& part : SplitString(expression, '|'))
  {
    const std::optional<std::string> label = GetChoiceLabel(StripWhitespace(part));
    if (!label)
    {
      return ReplaceAll(ReplaceAll(expression, "`", ""), "Horizon/0/Touchscreen:", "");
    }

    if (!description.empty())
      description += " or ";
    description += *label;
  }
  return description;
}

class InputCapture
{
public:
  static void Start(const std::string& control, std::function<void(const std::string&)> done)
  {
    new InputCapture(control, std::move(done));
  }

private:
  enum class Phase
  {
    WaitForRelease,
    Listen,
    WaitForFinalRelease,
  };

  InputCapture(const std::string& control, std::function<void(const std::string&)> done)
      : m_control(control), m_done(std::move(done))
  {
    padInitializeAny(&m_pad);

    m_label = new brls::Label();
    m_label->setHorizontalAlign(brls::HorizontalAlign::CENTER);
    m_label->setMargins(32, 32, 32, 32);
    UpdateLabel(CAPTURE_TIME.count());

    auto* content = new brls::Box(brls::Axis::COLUMN);
    content->addView(m_label);

    m_dialog = new brls::Dialog(content);
    m_dialog->setCancelable(false);
    m_dialog->open();

    brls::Application::blockInputs(true);
    m_subscription = brls::Application::getRunLoopEvent()->subscribe([this] { Poll(); });
  }

  void UpdateLabel(long seconds)
  {
    m_shown_seconds = seconds;
    m_label->setText(fmt::format("Press a button or move a stick for\n{}\n\n{}", m_control,
                                 seconds > 0 ? std::to_string(seconds) : std::string()));
  }

  void Poll()
  {
    if (m_finished)
      return;

    padUpdate(&m_pad);
    const std::optional<std::string> pressed = ciface::Horizon::GetPressedInput(m_pad);
    const auto now = std::chrono::steady_clock::now();

    switch (m_phase)
    {
    case Phase::WaitForRelease:
      if (!pressed)
      {
        m_phase = Phase::Listen;
        m_deadline = now + CAPTURE_TIME;
      }
      break;
    case Phase::Listen:
    {
      if (pressed)
      {
        m_result = fmt::format("`{}`", *pressed);
        m_phase = Phase::WaitForFinalRelease;
        UpdateLabel(0);
        break;
      }

      if (now >= m_deadline)
      {
        Finish();
        break;
      }

      const auto remaining = std::chrono::ceil<std::chrono::seconds>(m_deadline - now).count();
      if (remaining != m_shown_seconds)
        UpdateLabel(remaining);
      break;
    }
    case Phase::WaitForFinalRelease:
      if (!pressed)
        Finish();
      break;
    }
  }

  void Finish()
  {
    m_finished = true;

    brls::sync([this] {
      brls::Application::getRunLoopEvent()->unsubscribe(m_subscription);
      brls::Application::unblockInputs();
      m_dialog->close([done = std::move(m_done), result = std::move(m_result)] {
        if (result)
          done(*result);
      });
      delete this;
    });
  }

  PadState m_pad{};
  std::string m_control;
  std::function<void(const std::string&)> m_done;
  brls::Dialog* m_dialog = nullptr;
  brls::Label* m_label = nullptr;
  brls::VoidEvent::Subscription m_subscription;
  Phase m_phase = Phase::WaitForRelease;
  std::chrono::steady_clock::time_point m_deadline;
  long m_shown_seconds = 0;
  std::optional<std::string> m_result;
  bool m_finished = false;
};

class ProfileEditorView final : public brls::ScrollingFrame
{
public:
  ProfileEditorView(Kind kind, std::string name)
      : m_kind(kind), m_name(std::move(name)),
        m_controller(ControllerProfiles::CreateController(kind))
  {
    ControllerProfiles::Load(m_kind, m_name, *m_controller);

    m_list = CreateListBox();
    AddContainer(*m_controller, "", m_list);
    setContentView(m_list);

    Refresh();
  }

  ~ProfileEditorView() override
  {
    if (m_dirty && !ControllerProfiles::Save(m_kind, m_name, *m_controller))
      brls::Application::notify(fmt::format("Could not save {}.", m_name));
  }

  void ConfirmReset()
  {
    const std::string preset = ControllerProfiles::GetPreset(m_kind, m_name);
    Confirm(fmt::format("Reset every binding and setting in {} to {}?", m_name, preset), "Reset",
            [this, preset] {
              ControllerProfiles::Load(m_kind, preset, *m_controller);
              Changed();
            });
  }

  void Changed()
  {
    m_dirty = true;
    Refresh();
  }

  void MarkDirty() { m_dirty = true; }
  bool IsRefreshing() const { return m_refreshing; }

private:
  void Refresh()
  {
    m_refreshing = true;
    for (const std::function<void()>& refresh : m_refreshers)
      refresh();
    m_refreshing = false;
  }

  static std::string GetGroupTitle(const ControlGroup& group, const std::string& prefix)
  {
    std::string title;
    switch (group.type)
    {
    case GroupType::Cursor:
      title = "Pointer";
      break;
    case GroupType::IMUCursor:
      title = "Motion pointer";
      break;
    default:
      title = group.ui_name;
      break;
    }
    return prefix.empty() ? title : fmt::format("{}: {}", prefix, title);
  }

  static bool IsHidden(const ControlGroup& group)
  {
    if (group.type == GroupType::IRPassthrough || group.type == GroupType::IMUGyroscope)
      return true;
    return group.name == "Hotkeys" || group.name == GCPad::TRIFORCE_GROUP ||
           group.name == GCPad::MIC_GROUP;
  }

  void AddContainer(ControllerEmu::ControlGroupContainer& container, const std::string& prefix,
                    brls::Box* box)
  {
    ControlGroup* gyroscope = nullptr;
    ControllerEmu::Attachments* attachments = nullptr;
    for (const std::unique_ptr<ControlGroup>& group : container.groups)
    {
      if (group->type == GroupType::IMUGyroscope)
        gyroscope = group.get();
      else if (group->type == GroupType::Attachments)
        attachments = static_cast<ControllerEmu::Attachments*>(group.get());
    }

    if (attachments)
      AddExtensionSelector(*attachments, box);

    for (const std::unique_ptr<ControlGroup>& group : container.groups)
    {
      if (group->type == GroupType::IMUAccelerometer)
        AddMotion(*group, gyroscope, prefix, box);
      else if (group->type != GroupType::Attachments && !IsHidden(*group))
        AddGroup(*group, prefix, box);
    }

    if (attachments)
      AddExtensions(*attachments, box);
  }

  void AddGroup(ControlGroup& group, const std::string& prefix, brls::Box* box)
  {
    std::vector<ControllerEmu::NumericSettingBase*> settings;
    if (group.HasEnabledSetting())
      settings.push_back(group.enabled_setting.get());
    for (const auto& setting : group.numeric_settings)
    {
      if (setting->GetVisibility() == ControllerEmu::SettingVisibility::Normal)
        settings.push_back(setting.get());
    }

    if (group.controls.empty() && settings.empty())
      return;

    AddHeader(box, GetGroupTitle(group, prefix));

    if (group.type == GroupType::Stick)
      AddSourceSelector(group, "Use", STICK_SOURCES, box);
    else if (group.type == GroupType::Cursor)
      AddSourceSelector(group, "Use", POINTER_SOURCES, box);
    else if (group.name == GCPad::DPAD_GROUP && group.controls.size() == 4)
      AddSourceSelector(group, "Use", DPAD_SOURCES, box);

    if (group.type == GroupType::Shake)
    {
      std::vector<ControlReference*> references;
      for (const auto& control : group.controls)
        references.push_back(control->control_ref.get());
      AddBinding(group.ui_name, std::move(references), box);
    }
    else
    {
      for (const auto& control : group.controls)
        AddBinding(control->ui_name, {control->control_ref.get()}, box);
    }

    for (ControllerEmu::NumericSettingBase* setting : settings)
      AddSetting(*setting, box);
  }

  void AddBinding(const std::string& title, std::vector<ControlReference*> references,
                  brls::Box* box);

  void AddSourceSelector(ControlGroup& group, const std::string& title,
                         const std::vector<Source>& sources, brls::Box* box)
  {
    std::vector<std::string> labels;
    for (const Source& source : sources)
      labels.push_back(source.label);
    labels.push_back("Custom");

    const auto current = [&group, &sources] {
      for (size_t i = 0; i < sources.size(); ++i)
      {
        bool matches = true;
        for (size_t direction = 0; direction < 4; ++direction)
        {
          matches &= group.controls[direction]->control_ref->GetExpression() ==
                     sources[i].expressions[direction];
        }
        if (matches)
          return static_cast<int>(i);
      }
      return static_cast<int>(sources.size());
    };

    auto* cell = new brls::SelectorCell();
    cell->init(title, labels, current(), [this, &group, &sources](int index) {
      if (IsRefreshing() || index >= static_cast<int>(sources.size()))
        return;
      for (size_t direction = 0; direction < 4; ++direction)
        group.controls[direction]->control_ref->SetExpression(
            sources[index].expressions[direction]);
      Changed();
    });
    m_refreshers.push_back([cell, current] { cell->setSelection(current(), true); });
    box->addView(cell);
  }

  void AddMotion(ControlGroup& accelerometer, ControlGroup* gyroscope, const std::string& prefix,
                 brls::Box* box)
  {
    const auto expressions = [](const MotionSource& source, const auto& inputs) {
      std::vector<std::string> result;
      for (const char* input : inputs)
        result.push_back(source.prefix ? fmt::format("`{}{}`", source.prefix, input) : "");
      return result;
    };

    const auto matches = [](const ControlGroup& group, const std::vector<std::string>& wanted) {
      for (size_t i = 0; i < wanted.size() && i < group.controls.size(); ++i)
      {
        if (group.controls[i]->control_ref->GetExpression() != wanted[i])
          return false;
      }
      return true;
    };

    const auto current = [&accelerometer, gyroscope, expressions, matches] {
      for (size_t i = 0; i < MOTION_SOURCES.size(); ++i)
      {
        if (matches(accelerometer, expressions(MOTION_SOURCES[i], ACCEL_INPUTS)) &&
            (!gyroscope || matches(*gyroscope, expressions(MOTION_SOURCES[i], GYRO_INPUTS))))
        {
          return static_cast<int>(i);
        }
      }
      return static_cast<int>(MOTION_SOURCES.size());
    };

    std::vector<std::string> labels;
    for (const MotionSource& source : MOTION_SOURCES)
      labels.push_back(source.label);
    labels.push_back("Custom");

    AddHeader(box, prefix.empty() ? std::string("Motion") : fmt::format("{}: Motion", prefix));

    auto* cell = new brls::SelectorCell();
    cell->init("Motion sensor", labels, current(),
               [this, &accelerometer, gyroscope, expressions](int index) {
                 if (IsRefreshing() || index >= static_cast<int>(MOTION_SOURCES.size()))
                   return;

                 const auto bind = [](ControlGroup& group, const std::vector<std::string>& wanted) {
                   for (size_t i = 0; i < wanted.size() && i < group.controls.size(); ++i)
                     group.controls[i]->control_ref->SetExpression(wanted[i]);
                 };
                 bind(accelerometer, expressions(MOTION_SOURCES[index], ACCEL_INPUTS));
                 if (gyroscope)
                   bind(*gyroscope, expressions(MOTION_SOURCES[index], GYRO_INPUTS));
                 Changed();
               });
    m_refreshers.push_back([cell, current] { cell->setSelection(current(), true); });
    box->addView(cell);

    AddNote(box, "Either Joy-Con can drive motion when paired. Sideways Joy-Con is for a "
                 "single Joy-Con held on its side.");
  }

  void AddExtensionSelector(ControllerEmu::Attachments& attachments, brls::Box* box)
  {
    std::vector<std::string> labels;
    for (const auto& attachment : attachments.GetAttachmentList())
      labels.push_back(attachment->GetDisplayName());

    AddHeader(box, "Extension");

    auto* cell = new brls::SelectorCell();
    cell->init("Extension", labels, static_cast<int>(attachments.GetSelectedAttachment()),
               [this, &attachments](int index) {
                 if (IsRefreshing())
                   return;
                 attachments.SetSelectedAttachment(index);
                 Changed();
               });
    m_refreshers.push_back([cell, &attachments] {
      cell->setSelection(static_cast<int>(attachments.GetSelectedAttachment()), true);
    });
    box->addView(cell);

    for (const auto& setting : attachments.numeric_settings)
      AddSetting(*setting, box);
  }

  void AddExtensions(ControllerEmu::Attachments& attachments, brls::Box* box)
  {
    const auto& list = attachments.GetAttachmentList();
    for (size_t i = 0; i < list.size(); ++i)
    {
      auto* section = new brls::Box(brls::Axis::COLUMN);
      AddContainer(*list[i], list[i]->GetDisplayName(), section);
      if (section->getChildren().empty())
      {
        delete section;
        continue;
      }

      section->setMarginTop(24);
      box->addView(section);
      m_refreshers.push_back([section, &attachments, i] {
        section->setVisibility(attachments.GetSelectedAttachment() == i ?
                                   brls::Visibility::VISIBLE :
                                   brls::Visibility::GONE);
      });
    }
  }

  void AddSetting(ControllerEmu::NumericSettingBase& setting, brls::Box* box)
  {
    if (!setting.IsSimpleValue())
      return;

    const std::string title = setting.GetUIName();

    if (setting.GetType() == ControllerEmu::SettingType::Bool)
    {
      auto& value = static_cast<ControllerEmu::NumericSetting<bool>&>(setting);
      auto* cell = new brls::BooleanCell();
      cell->init(title, value.GetValue(), [this, &value](bool on) {
        value.SetValue(on);
        MarkDirty();
      });
      m_refreshers.push_back([cell, &value] { cell->setOn(value.GetValue(), false); });
      box->addView(cell);
      return;
    }

    struct Range
    {
      std::function<double()> get;
      std::function<void(double)> set;
      double min;
      double max;
      bool integer;
    };

    Range range;
    if (setting.GetType() == ControllerEmu::SettingType::Int)
    {
      auto& value = static_cast<ControllerEmu::NumericSetting<int>&>(setting);
      range = {[&value] { return double(value.GetValue()); },
               [&value](double v) { value.SetValue(static_cast<int>(std::lround(v))); },
               double(value.GetMinValue()), double(value.GetMaxValue()), true};
    }
    else
    {
      auto& value = static_cast<ControllerEmu::NumericSetting<double>&>(setting);
      range = {[&value] { return value.GetValue(); }, [&value](double v) { value.SetValue(v); },
               value.GetMinValue(), value.GetMaxValue(),
               value.GetMaxValue() - value.GetMinValue() > 20};
    }

    if (range.max <= range.min)
      return;

    const std::string suffix = setting.GetUISuffix() ? setting.GetUISuffix() : "";
    const auto format = [range, suffix](double value) {
      return range.integer ? fmt::format("{:.0f}{}", value, suffix) :
                             fmt::format("{:.1f}{}", value, suffix);
    };
    const auto snap = [range](float progress) {
      const double value = range.min + progress * (range.max - range.min);
      return range.integer ? std::round(value) : std::round(value * 10) / 10;
    };
    const auto progress_of = [range] {
      return static_cast<float>((range.get() - range.min) / (range.max - range.min));
    };

    auto* cell = new brls::SliderCell();
    cell->init(title, progress_of(), [this, cell, range, format, snap](float progress) {
      if (IsRefreshing())
        return;
      const double value = snap(progress);
      range.set(value);
      cell->setDetailText(format(value));
      MarkDirty();
    });
    cell->setDetailText(format(range.get()));
    m_refreshers.push_back([cell, range, format, progress_of] {
      cell->slider->setProgress(progress_of());
      cell->setDetailText(format(range.get()));
    });
    box->addView(cell);
  }

  const Kind m_kind;
  const std::string m_name;
  std::unique_ptr<ControllerEmu::EmulatedController> m_controller;
  brls::Box* m_list = nullptr;
  std::vector<std::function<void()>> m_refreshers;
  bool m_dirty = false;
  bool m_refreshing = false;
};

class BindingCell final : public brls::DetailCell
{
public:
  BindingCell(ProfileEditorView& editor, std::string label,
              std::vector<ControlReference*> references)
      : m_editor(editor), m_title(std::move(label)), m_references(std::move(references)),
        m_is_input(m_references.front()->IsInput())
  {
    setText(m_title);

    registerClickAction([this](brls::View*) {
      if (m_is_input)
        Capture(false);
      else
        Choose();
      return true;
    });
    registerAction("Choose", brls::BUTTON_Y, [this](brls::View*) {
      Choose();
      return true;
    });
    registerAction("Clear", brls::BUTTON_X, [this](brls::View*) {
      Set("");
      return true;
    });
    if (m_is_input)
    {
      registerAction("Add alternative", brls::BUTTON_RB, [this](brls::View*) {
        Capture(true);
        return true;
      });
    }

    Refresh();
  }

  void Refresh()
  {
    const std::string expression = m_references.front()->GetExpression();

    bool uniform = true;
    for (const ControlReference* reference : m_references)
      uniform &= reference->GetExpression() == expression;

    setDetailText(uniform ? DescribeExpression(expression) : std::string("Custom"));
    setDetailTextColor(expression.empty() ? ThemeColour("brls/text_disabled") :
                                            ThemeColour("brls/list/listItem_value_color"));
  }

private:
  void Set(const std::string& expression)
  {
    for (ControlReference* reference : m_references)
      reference->SetExpression(expression);
    m_editor.Changed();
  }

  void Capture(bool add)
  {
    InputCapture::Start(m_title, [this, add](const std::string& input) {
      const std::string current = m_references.front()->GetExpression();
      if (!add || current.empty())
        Set(input);
      else if (current.find(input) == std::string::npos)
        Set(fmt::format("{} | {}", current, input));
    });
  }

  void Choose()
  {
    const std::string current = m_references.front()->GetExpression();

    std::vector<std::string> labels{"Unbound"};
    std::vector<std::string> expressions{""};
    const auto add = [&](const auto& choices) {
      for (const InputChoice& choice : choices)
      {
        labels.push_back(choice.label);
        expressions.push_back(choice.expression);
      }
    };
    if (m_is_input)
      add(INPUT_CHOICES);
    else
      add(OUTPUT_CHOICES);

    int selected = 0;
    for (size_t i = 0; i < expressions.size(); ++i)
    {
      if (expressions[i] == current)
        selected = static_cast<int>(i);
    }

    ShowDropdown(m_title, labels, selected,
                 [this, expressions](int index) { Set(expressions[index]); });
  }

  ProfileEditorView& m_editor;
  const std::string m_title;
  const std::vector<ControlReference*> m_references;
  const bool m_is_input;
};

void ProfileEditorView::AddBinding(const std::string& title,
                                   std::vector<ControlReference*> references, brls::Box* box)
{
  auto* cell = new BindingCell(*this, title, std::move(references));
  m_refreshers.push_back([cell] { cell->Refresh(); });
  box->addView(cell);
}

brls::Activity* CreateEditorActivity(Kind kind, const std::string& name)
{
  auto* editor = new ProfileEditorView(kind, name);

  auto* frame = new brls::AppletFrame(editor);
  frame->setTitle(name);
  frame->registerAction("Reset to preset", brls::BUTTON_BACK, [editor](brls::View*) {
    editor->ConfirmReset();
    return true;
  });
  return new brls::Activity(frame);
}

class ProfileCell final : public brls::DetailCell
{
public:
  ProfileCell(const std::string& label, Kind kind, ProfileBinding binding)
      : m_title(label), m_kind(kind), m_binding(std::move(binding))
  {
    setText(m_title);
    registerClickAction([this](brls::View*) {
      Choose();
      return true;
    });
    Refresh();
  }

  void onFocusGained() override
  {
    brls::DetailCell::onFocusGained();
    Refresh();
  }

private:
  bool IsPerGame() const { return static_cast<bool>(m_binding.clear); }
  bool IsFollowingDefault() const { return IsPerGame() && !m_binding.overridden(); }

  void Refresh()
  {
    if (IsFollowingDefault())
    {
      setDetailText(fmt::format("Default ({})", m_binding.inherited()));
      setDetailTextColor(ThemeColour("brls/text_disabled"));
    }
    else
    {
      setDetailText(m_binding.current());
      setDetailTextColor(ThemeColour("brls/list/listItem_value_color"));
    }
  }

  void Choose()
  {
    std::vector<std::string> names = ControllerProfiles::GetPresetNames(m_kind);
    for (std::string& name : ControllerProfiles::GetUserProfileNames(m_kind))
      names.push_back(std::move(name));

    std::vector<std::string> labels;
    if (IsPerGame())
      labels.push_back(fmt::format("Default ({})", m_binding.inherited()));
    const int offset = static_cast<int>(labels.size());
    labels.insert(labels.end(), names.begin(), names.end());

    int selected = 0;
    if (!IsFollowingDefault())
    {
      const std::string current = m_binding.current();
      for (size_t i = 0; i < names.size(); ++i)
      {
        if (names[i] == current)
          selected = static_cast<int>(i) + offset;
      }
    }

    ShowDropdown(m_title, labels, selected, [this, names, offset](int index) {
      if (index < offset)
        m_binding.clear();
      else
        m_binding.write(names[index - offset]);
      Refresh();
    });
  }

  const std::string m_title;
  const Kind m_kind;
  ProfileBinding m_binding;
};

class ProfileManagerView final : public brls::ScrollingFrame
{
public:
  explicit ProfileManagerView(Kind kind) : m_kind(kind)
  {
    m_list = CreateListBox();
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

  ~ProfileManagerView() override
  {
    brls::Application::getRunLoopEvent()->unsubscribe(m_subscription);
  }

private:
  brls::DetailCell* AddCell(const std::string& title, const std::string& detail,
                            std::function<void()> action)
  {
    auto* cell = new brls::DetailCell();
    cell->setText(title);
    cell->setDetailText(detail);
    cell->registerClickAction([action = std::move(action)](brls::View*) {
      action();
      return true;
    });
    m_list->addView(cell);
    return cell;
  }

  void ScheduleRebuild(std::string focus)
  {
    m_focus = std::move(focus);
    m_rebuild_pending = true;
  }

  void Rebuild(bool give_focus)
  {
    m_rebuild_pending = false;
    m_list->clearViews();

    brls::View* focus = AddCell("New profile", "", [this] { NewProfile(); });

    AddHeader(m_list, "Presets");
    for (const std::string& name : ControllerProfiles::GetPresetNames(m_kind))
    {
      brls::DetailCell* cell = AddCell(name, "Preset", [this, name] {
        Confirm("Presets cannot be changed. Make a copy to edit?", "Copy",
                [this, name] { Copy(name, true); });
      });
      if (name == m_focus)
        focus = cell;
    }

    AddHeader(m_list, "Your profiles");
    const std::vector<std::string> names = ControllerProfiles::GetUserProfileNames(m_kind);
    if (names.empty())
      AddNote(m_list, "None yet. Choose New profile, or copy a preset.");

    for (const std::string& name : names)
    {
      brls::DetailCell* cell = AddCell(name, "", [this, name] {
        brls::Application::pushActivity(CreateEditorActivity(m_kind, name));
      });
      cell->registerAction("Duplicate", brls::BUTTON_X, [this, name](brls::View*) {
        Copy(name, false);
        return true;
      });
      cell->registerAction("Rename", brls::BUTTON_Y, [this, name](brls::View*) {
        RenameProfile(name);
        return true;
      });
      cell->registerAction("Delete", brls::BUTTON_BACK, [this, name](brls::View*) {
        DeleteProfile(name);
        return true;
      });
      if (name == m_focus)
        focus = cell;
    }

    AddNote(m_list, fmt::format("Choose which profile each {} uses on the Controls page.",
                                ControllerProfiles::GetKindLabel(m_kind)));

    if (give_focus)
      brls::Application::giveFocus(focus);
  }

  std::string SuggestName(const std::string& base) const
  {
    std::string stem = base;
    if (ControllerProfiles::IsPreset(m_kind, base))
      stem = "My " + base.substr(base.find(" - ") + 3);

    std::string name = stem;
    for (int i = 2; !ControllerProfiles::CheckNewName(m_kind, name).empty(); ++i)
      name = fmt::format("{} {}", stem, i);
    return name;
  }

  void PromptName(const std::string& initial, std::function<void(const std::string&)> done)
  {
    const bool opened = brls::Application::getImeManager()->openForText(
        [this, done = std::move(done)](std::string name) {
          const std::string problem = ControllerProfiles::CheckNewName(m_kind, name);
          if (!problem.empty())
          {
            brls::Application::notify(problem);
            return;
          }
          done(name);
        },
        "Profile name", "", 48, initial);

    if (!opened)
      brls::Application::notify("The keyboard could not be opened.");
  }

  void NewProfile()
  {
    const std::vector<std::string> presets = ControllerProfiles::GetPresetNames(m_kind);
    if (presets.size() == 1)
    {
      Copy(presets.front(), true);
      return;
    }

    ShowDropdown("Start from", presets, 0,
                 [this, presets](int index) { Copy(presets[index], true); });
  }

  void Copy(const std::string& source, bool edit)
  {
    PromptName(SuggestName(source), [this, source, edit](const std::string& name) {
      if (!ControllerProfiles::Create(m_kind, name, source))
      {
        brls::Application::notify(fmt::format("Could not create {}.", name));
        return;
      }

      ScheduleRebuild(name);
      if (edit)
        brls::Application::pushActivity(CreateEditorActivity(m_kind, name));
    });
  }

  void RenameProfile(const std::string& name)
  {
    PromptName(name, [this, name](const std::string& new_name) {
      if (!ControllerProfiles::Rename(m_kind, name, new_name))
      {
        brls::Application::notify(fmt::format("Could not rename {}.", name));
        return;
      }
      ScheduleRebuild(new_name);
    });
  }

  void DeleteProfile(const std::string& name)
  {
    Confirm(fmt::format("Delete {}? Anything using it goes back to the default layout.", name), "Delete",
            [this, name] {
              if (!ControllerProfiles::Delete(m_kind, name))
              {
                brls::Application::notify(fmt::format("Could not delete {}.", name));
                return;
              }
              ScheduleRebuild("");
            });
  }

  const Kind m_kind;
  brls::Box* m_list = nullptr;
  brls::VoidEvent::Subscription m_subscription;
  std::string m_focus;
  bool m_rebuild_pending = false;
};
}  // namespace

brls::View* CreateProfileCell(const std::string& title, Kind kind, ProfileBinding binding)
{
  return new ProfileCell(title, kind, std::move(binding));
}

brls::Activity* CreateProfileManagerActivity(Kind kind)
{
  auto* frame = new brls::AppletFrame(new ProfileManagerView(kind));
  frame->setTitle(fmt::format("{} profiles", ControllerProfiles::GetKindLabel(kind)));
  return new brls::Activity(frame);
}
}  // namespace Shell
