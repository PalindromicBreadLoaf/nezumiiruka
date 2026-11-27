// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "InputCommon/ControllerInterface/Horizon/Horizon.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

#include <switch.h>

#include <fmt/format.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"
#include "Common/MathUtil.h"

#include "InputCommon/ControllerInterface/ControllerInterface.h"

namespace ciface::Horizon
{
namespace
{
const std::string SOURCE = "Horizon";

constexpr int MAX_PLAYERS = 8;

// The touch panel reports in its own fixed coordinate space regardless of docked status.
constexpr ControlState TOUCH_WIDTH = 1280;
constexpr ControlState TOUCH_HEIGHT = 720;

// HID reports acceleration in g and angular velocity in rotations per second.
// Dolphin's wants m/s^2 and rad/s.
constexpr ControlState GRAVITY = 9.80665;

constexpr ControlState DETECT_THRESHOLD = 0.6;

// Frequencies the Joy-Con LRA is happiest at apparently?
constexpr float VIBRATION_FREQ_LOW = 160.0f;
constexpr float VIBRATION_FREQ_HIGH = 320.0f;

#ifdef __SWITCH2__
constexpr s32 MOUSE_AREA_WIDTH = 1920;
constexpr s32 MOUSE_AREA_HEIGHT = 1080;
constexpr s32 MOUSE_ENGAGE_TRAVEL = 64;
constexpr auto MOUSE_IDLE_TIMEOUT = std::chrono::seconds(3);
constexpr float MOUSE_GRIP_MIN_ACCEL = 0.85f;
constexpr float MOUSE_GRIP_MAX_ACCEL = 1.15f;

struct MousePointer
{
  std::mutex mutex;
  u64 serial = 0;
  ControlState x = 0;
  ControlState y = 0;
  std::array<bool, MAX_PLAYERS> engaged{};
};

MousePointer s_mouse_pointer;
std::atomic<bool> s_mouse_pointer_enabled = true;
std::atomic<bool> s_mouse_pointer_left = false;
#endif

struct ButtonDef
{
  const char* name;
  u64 mask;
};

constexpr std::array BUTTONS{
    ButtonDef{"A", HidNpadButton_A},
    ButtonDef{"B", HidNpadButton_B},
    ButtonDef{"X", HidNpadButton_X},
    ButtonDef{"Y", HidNpadButton_Y},
    ButtonDef{"L", HidNpadButton_L},
    ButtonDef{"R", HidNpadButton_R},
    ButtonDef{"ZL", HidNpadButton_ZL},
    ButtonDef{"ZR", HidNpadButton_ZR},
    ButtonDef{"Plus", HidNpadButton_Plus},
    ButtonDef{"Minus", HidNpadButton_Minus},
    ButtonDef{"Stick L", HidNpadButton_StickL},
    ButtonDef{"Stick R", HidNpadButton_StickR},
    ButtonDef{"Pad Up", HidNpadButton_Up},
    ButtonDef{"Pad Down", HidNpadButton_Down},
    ButtonDef{"Pad Left", HidNpadButton_Left},
    ButtonDef{"Pad Right", HidNpadButton_Right},
    ButtonDef{"SL", HidNpadButton_AnySL},
    ButtonDef{"SR", HidNpadButton_AnySR},
};

constexpr std::array SIDEWAYS_BUTTONS{
    "Sideways Right", "Sideways Down", "Sideways Left",
    "Sideways Up",    "Sideways Menu", "Sideways Stick Click",
};

// A given Npad reports exactly one style at a time.
constexpr std::array<HidNpadStyleTag, 6> STYLES{
    HidNpadStyleTag_NpadFullKey, HidNpadStyleTag_NpadHandheld, HidNpadStyleTag_NpadJoyDual,
    HidNpadStyleTag_NpadJoyLeft, HidNpadStyleTag_NpadJoyRight, HidNpadStyleTag_NpadGc,
};

bool SupportsTwoVibrationDevices(HidNpadStyleTag style)
{
  return style == HidNpadStyleTag_NpadFullKey || style == HidNpadStyleTag_NpadHandheld ||
         style == HidNpadStyleTag_NpadJoyDual;
}

class Button final : public Core::Device::Input
{
public:
  Button(const char* name, const u64& buttons, u64 mask)
      : m_name(name), m_buttons(buttons), m_mask(mask)
  {
  }

  std::string GetName() const override { return m_name; }
  ControlState GetState() const override { return (m_buttons & m_mask) != 0; }

private:
  const char* const m_name;
  const u64& m_buttons;
  const u64 m_mask;
};

// One half of an axis. scale selects the direction and carries the unit conversion.
class Axis final : public Core::Device::Input
{
public:
  Axis(std::string name, const ControlState& value, ControlState scale, bool detectable = true)
      : m_name(std::move(name)), m_value(value), m_scale(scale), m_detectable(detectable)
  {
  }

  std::string GetName() const override { return m_name; }
  bool IsDetectable() const override { return m_detectable; }
  ControlState GetState() const override { return m_value * m_scale; }

private:
  const std::string m_name;
  const ControlState& m_value;
  const ControlState m_scale;
  const bool m_detectable;
};

class Trigger final : public Core::Device::Input
{
public:
  Trigger(const char* name, const ControlState& value) : m_name(name), m_value(value) {}

  std::string GetName() const override { return m_name; }
  ControlState GetState() const override { return m_value; }

private:
  const char* const m_name;
  const ControlState& m_value;
};

class Motor final : public Core::Device::Output
{
public:
  Motor(const char* name, std::atomic<ControlState>* low, std::atomic<ControlState>* high)
      : m_name(name), m_low(low), m_high(high)
  {
  }

  std::string GetName() const override { return m_name; }

  void SetState(ControlState state) override
  {
    if (m_low)
      m_low->store(state, std::memory_order_relaxed);
    if (m_high)
      m_high->store(state, std::memory_order_relaxed);
  }

private:
  const char* const m_name;
  std::atomic<ControlState>* const m_low;
  std::atomic<ControlState>* const m_high;
};

class NpadDevice final : public Core::Device
{
public:
  explicit NpadDevice(int player_index)
      : m_player_index(player_index),
        m_npad_id(static_cast<HidNpadIdType>(HidNpadIdType_No1 + player_index))
  {
    // The same Joy-Cons switch between the two ids as they are attached and detached.
    u64 mask = 1ULL << m_npad_id;
    if (player_index == 0)
      mask |= 1ULL << HidNpadIdType_Handheld;
    padInitializeWithMask(&m_pad, mask);

    for (const ButtonDef& button : BUTTONS)
      AddInput(new Button(button.name, m_buttons, button.mask));

    AddStick("Left", m_stick[0], m_stick[1]);
    AddStick("Right", m_stick[2], m_stick[3]);
    AddSidewaysInputs();

    // Only ::HidNpadStyleTag_NpadGc fills these in.
    AddInput(new Trigger("Trigger L", m_trigger[0]));
    AddInput(new Trigger("Trigger R", m_trigger[1]));

    AddMotionInputs("", m_accel, m_gyro);
    AddMotionInputs("Left ", m_left_accel, m_left_gyro);
    AddMotionInputs("Sideways ", m_sideways_accel, m_sideways_gyro);

    AddOutput(new Motor("Motor", &m_rumble_low, &m_rumble_high));
    AddOutput(new Motor("Motor L", &m_rumble_low, nullptr));
    AddOutput(new Motor("Motor R", nullptr, &m_rumble_high));
  }

  ~NpadDevice() override
  {
    ReleaseHandles();
#ifdef __SWITCH2__
    StopMouse();
#endif
  }

  std::string GetName() const override { return fmt::format("Player {}", m_player_index + 1); }
  std::string GetSource() const override { return SOURCE; }

  // Player one has to sort first.
  int GetSortPriority() const override { return -m_player_index; }

  Core::DeviceRemoval UpdateInput() override
  {
    padUpdate(&m_pad);

    m_buttons = padGetButtons(&m_pad);

    const HidAnalogStickState left = padGetStickPos(&m_pad, 0);
    const HidAnalogStickState right = padGetStickPos(&m_pad, 1);
    m_stick[0] = ControlState(left.x) / JOYSTICK_MAX;
    m_stick[1] = ControlState(left.y) / JOYSTICK_MAX;
    m_stick[2] = ControlState(right.x) / JOYSTICK_MAX;
    m_stick[3] = ControlState(right.y) / JOYSTICK_MAX;

    m_trigger[0] = ControlState(padGetGcTriggerPos(&m_pad, 0)) / 0x7fff;
    m_trigger[1] = ControlState(padGetGcTriggerPos(&m_pad, 1)) / 0x7fff;

    RefreshHandles();
    UpdateSideways();
#ifdef __SWITCH2__
    UpdateMouse();
#endif
    UpdateMotion();
    UpdateRumble();

    return Core::DeviceRemoval::Keep;
  }

private:
  void AddStick(const char* name, ControlState& x, ControlState& y)
  {
    AddFullAnalogSurfaceInputs(new Axis(fmt::format("{} X-", name), x, -1),
                               new Axis(fmt::format("{} X+", name), x, 1));
    AddFullAnalogSurfaceInputs(new Axis(fmt::format("{} Y-", name), y, -1),
                               new Axis(fmt::format("{} Y+", name), y, 1));
  }

  void AddSidewaysInputs()
  {
    for (size_t i = 0; i < SIDEWAYS_BUTTONS.size(); ++i)
      AddInput(new Button(SIDEWAYS_BUTTONS[i], m_sideways_buttons, 1ULL << i));

    AddStick("Sideways Stick", m_sideways_stick[0], m_sideways_stick[1]);
  }

  void UpdateSideways()
  {
    const auto pick = [this](std::initializer_list<u64> masks) {
      u64 buttons = 0;
      u64 bit = 1;
      for (const u64 mask : masks)
      {
        if (m_buttons & mask)
          buttons |= bit;
        bit <<= 1;
      }
      return buttons;
    };

    switch (m_handle_style)
    {
    case HidNpadStyleTag_NpadJoyLeft:
      m_sideways_buttons = pick({HidNpadButton_Down, HidNpadButton_Left, HidNpadButton_Up,
                                 HidNpadButton_Right, HidNpadButton_Minus, HidNpadButton_StickL});
      m_sideways_stick[0] = -m_stick[1];
      m_sideways_stick[1] = m_stick[0];
      break;
    case HidNpadStyleTag_NpadJoyRight:
      m_sideways_buttons = pick({HidNpadButton_X, HidNpadButton_A, HidNpadButton_B, HidNpadButton_Y,
                                 HidNpadButton_Plus, HidNpadButton_StickR});
      m_sideways_stick[0] = m_stick[3];
      m_sideways_stick[1] = -m_stick[2];
      break;
    default:
      m_sideways_buttons = pick({HidNpadButton_A, HidNpadButton_B, HidNpadButton_Y, HidNpadButton_X,
                                 HidNpadButton_Plus, HidNpadButton_StickL});
      m_sideways_stick[0] = m_stick[0];
      m_sideways_stick[1] = m_stick[1];
      break;
    }
  }

  // Without this the emulated Wii Remote is upside down, which is very noticeable in gameplay.
  void AddMotionInputs(const std::string& prefix, const std::array<ControlState, 3>& accel,
                       const std::array<ControlState, 3>& gyro)
  {
    const auto add = [&](const char* name, const ControlState& value, ControlState scale) {
      AddInput(new Axis(prefix + name, value, scale, false));
    };

    add("Accel Up", accel[2], -GRAVITY);
    add("Accel Down", accel[2], GRAVITY);
    add("Accel Left", accel[1], -GRAVITY);
    add("Accel Right", accel[1], GRAVITY);
    add("Accel Forward", accel[0], -GRAVITY);
    add("Accel Backward", accel[0], GRAVITY);

    add("Gyro Pitch Up", gyro[1], -MathUtil::TAU);
    add("Gyro Pitch Down", gyro[1], MathUtil::TAU);
    add("Gyro Roll Left", gyro[0], -MathUtil::TAU);
    add("Gyro Roll Right", gyro[0], MathUtil::TAU);
    add("Gyro Yaw Left", gyro[2], MathUtil::TAU);
    add("Gyro Yaw Right", gyro[2], -MathUtil::TAU);
  }

  static std::array<ControlState, 3> ToSideways(HidNpadStyleTag style,
                                                const std::array<ControlState, 3>& v)
  {
    switch (style)
    {
    case HidNpadStyleTag_NpadJoyLeft:
      return v;
    case HidNpadStyleTag_NpadJoyRight:
      return {-v[0], -v[1], v[2]};
    default:
      return {v[1], -v[0], v[2]};
    }
  }

  HidNpadIdType ActiveId() const
  {
    return padIsHandheld(&m_pad) ? HidNpadIdType_Handheld : m_npad_id;
  }

  // Docking, undocking and re-attaching Joy-Cons all change the style, and every sixaxis and
  // vibration handle is style-specific.
  void RefreshHandles()
  {
    const HidNpadIdType id = ActiveId();
    const u32 style_set = hidGetNpadStyleSet(id);

    HidNpadStyleTag style = HidNpadStyleTag{};
    for (const HidNpadStyleTag candidate : STYLES)
    {
      if (style_set & candidate)
      {
        style = candidate;
        break;
      }
    }

    if (id == m_handle_id && style == m_handle_style)
      return;

    ReleaseHandles();
    m_handle_id = id;
    m_handle_style = style;
    if (style == HidNpadStyleTag{})
      return;

    const s32 sixaxis_count = style == HidNpadStyleTag_NpadJoyDual ? 2 : 1;
    if (R_SUCCEEDED(hidGetSixAxisSensorHandles(m_sixaxis.data(), sixaxis_count, id, style)))
    {
      m_sixaxis_count = sixaxis_count;
      m_log_sensor_sample = true;
      for (s32 i = 0; i < sixaxis_count; ++i)
        hidStartSixAxisSensor(m_sixaxis[i]);
    }
    else
    {
      WARN_LOG_FMT(CONTROLLERINTERFACE, "{}: no sixaxis handles for style {:#x}", GetName(),
                   static_cast<u32>(style));
    }

    const s32 vibration_count = SupportsTwoVibrationDevices(style) ? 2 : 1;
    if (R_SUCCEEDED(hidInitializeVibrationDevices(m_vibration.data(), vibration_count, id, style)))
    {
      m_vibration_count = vibration_count;

      HidVibrationDeviceInfo info{};
      m_vibration_is_gc_erm = R_SUCCEEDED(hidGetVibrationDeviceInfo(m_vibration[0], &info)) &&
                              info.type == HidVibrationDeviceType_GcErm;
    }
    else
    {
      WARN_LOG_FMT(CONTROLLERINTERFACE, "{}: no vibration handles for style {:#x}", GetName(),
                   static_cast<u32>(style));
    }
  }

  void ReleaseHandles()
  {
    StopRumble();

    for (s32 i = 0; i < m_sixaxis_count; ++i)
      hidStopSixAxisSensor(m_sixaxis[i]);

    m_sixaxis_count = 0;
    m_vibration_count = 0;
    m_vibration_is_gc_erm = false;
  }

  static bool ReadMotion(HidSixAxisSensorHandle handle, std::array<ControlState, 3>& accel,
                         std::array<ControlState, 3>& gyro)
  {
    HidSixAxisSensorState state{};
    if (hidGetSixAxisSensorStates(handle, &state, 1) != 1)
      return false;

    accel = {state.acceleration.y, -state.acceleration.x, state.acceleration.z};
    gyro = {state.angular_velocity.y, -state.angular_velocity.x, state.angular_velocity.z};
    return true;
  }

  void UpdateMotion()
  {
#ifdef __SWITCH2__
    if (m_mouse_engaged)
    {
      m_accel = {0, 0, -1};
      m_gyro = {};
      m_left_accel = m_accel;
      m_left_gyro = m_gyro;
      m_sideways_accel = m_accel;
      m_sideways_gyro = m_gyro;
      return;
    }
#endif

    if (m_sixaxis_count == 0)
      return;

    if (!ReadMotion(m_sixaxis[m_sixaxis_count - 1], m_accel, m_gyro))
      return;

    if (m_sixaxis_count < 2 || !ReadMotion(m_sixaxis[0], m_left_accel, m_left_gyro))
    {
      m_left_accel = m_accel;
      m_left_gyro = m_gyro;
    }

    m_sideways_accel = ToSideways(m_handle_style, m_accel);
    m_sideways_gyro = ToSideways(m_handle_style, m_gyro);

    if (std::exchange(m_log_sensor_sample, false))
    {
      NOTICE_LOG_FMT(CONTROLLERINTERFACE,
                     "{}: sensor sample accel ({:.3f}, {:.3f}, {:.3f}) g, gyro "
                     "({:.3f}, {:.3f}, {:.3f}) rot/s",
                     GetName(), m_accel[0], m_accel[1], m_accel[2], m_gyro[0], m_gyro[1],
                     m_gyro[2]);
    }
  }

  // Outputs are written from the CPU thread as the game asks for rumble.
  void UpdateRumble()
  {
    if (m_vibration_count == 0)
      return;

    const auto low = static_cast<float>(m_rumble_low.load(std::memory_order_relaxed));
    const auto high = static_cast<float>(m_rumble_high.load(std::memory_order_relaxed));
    if (low == m_sent_low && high == m_sent_high)
      return;

    m_sent_low = low;
    m_sent_high = high;

    if (m_vibration_is_gc_erm)
    {
      const HidVibrationGcErmCommand command = (low > 0.5f || high > 0.5f) ?
                                                   HidVibrationGcErmCommand_Start :
                                                   HidVibrationGcErmCommand_Stop;
      hidSendVibrationGcErmCommand(m_vibration[0], command);
      return;
    }

    const HidVibrationValue value{low, VIBRATION_FREQ_LOW, high, VIBRATION_FREQ_HIGH};
    const std::array<HidVibrationValue, 2> values{value, value};
    hidSendVibrationValues(m_vibration.data(), values.data(), m_vibration_count);
  }

  void StopRumble()
  {
    m_rumble_low.store(0, std::memory_order_relaxed);
    m_rumble_high.store(0, std::memory_order_relaxed);

    // Force the next UpdateRumble to send so a device that is torn down mid-buzz goes quiet.
    // Or I could make it be funny and have it just lose it's shit until the battery dies.
    m_sent_low = -1;
    m_sent_high = -1;
    UpdateRumble();
  }

#ifdef __SWITCH2__
  struct MouseReading
  {
    bool active = false;
    s32 x = 0;
    s32 y = 0;
    s32 delta_x = 0;
    s32 delta_y = 0;
    s32 wheel_delta_y = 0;
    u32 buttons = 0;
  };

  bool IsInMouseGrip(bool left) const
  {
    if (m_sixaxis_count == 0)
      return true;

    const s32 index = m_sixaxis_count == 2 && !left ? 1 : 0;
    HidSixAxisSensorState state{};
    if (hidGetSixAxisSensorStates(m_sixaxis[index], &state, 1) != 1)
      return true;

    if (state.acceleration.x == 0 && state.acceleration.y == 0 && state.acceleration.z == -1)
      return true;

    const float x = left ? state.acceleration.x : -state.acceleration.x;
    return x >= MOUSE_GRIP_MIN_ACCEL && x <= MOUSE_GRIP_MAX_ACCEL;
  }

  std::optional<HidNmouseHandle> FindMouseHandle(bool left) const
  {
    const u32 wanted = left ? HidNmouseType_JoyLeft : HidNmouseType_JoyRight;
    for (const HidNpadIdType id : {m_npad_id, HidNpadIdType_Handheld})
    {
      if (id == HidNpadIdType_Handheld && m_player_index != 0)
        continue;

      std::array<HidNmouseHandle, 2> handles{};
      s32 found = 0;
      if (R_FAILED(hidGetNmouseHandles(handles.data(), s32(handles.size()), id, &found)))
        continue;

      for (s32 i = 0; i < std::min(found, s32(handles.size())); ++i)
      {
        u32 types = 0;
        if (R_SUCCEEDED(hidGetNmouseTypeSet(handles[i], &types)) && (types & wanted))
          return handles[i];
      }
    }
    return std::nullopt;
  }

  MouseReading ReadMouse(bool left)
  {
    MouseReading reading;
    const std::optional<HidNmouseHandle> handle = FindMouseHandle(left);

    std::array<HidNmouseStateWithDelta, 16> states{};
    size_t total = 0;
    if (!handle ||
        R_FAILED(hidGetNmouseStatesWithDelta(*handle, states.data(), states.size(), &total)) ||
        total == 0)
    {
      m_mouse_tracking = false;
      return reading;
    }

    const bool known = m_mouse_tracking && handle->handle == m_mouse_handle.handle;
    reading.active = true;
    reading.x = states[0].x;
    reading.y = states[0].y;
    reading.buttons = states[0].buttons;
    if (known)
    {
      for (size_t i = 0; i < std::min(total, states.size()); ++i)
      {
        if (states[i].sampling_number <= m_mouse_sampling_number)
          continue;
        reading.delta_x += states[i].delta_x;
        reading.delta_y += states[i].delta_y;
        reading.wheel_delta_y += states[i].wheel_delta_y;
      }
    }
    else
    {
      m_mouse_has_anchor = false;
    }

    m_mouse_tracking = true;
    m_mouse_handle = *handle;
    m_mouse_sampling_number = states[0].sampling_number;
    return reading;
  }

  void StopMouse()
  {
    if (m_mouse_ready)
      padPointerExit(&m_mouse_lifetime);
    m_mouse_ready = false;
    m_mouse_engaged = false;
    m_mouse_has_anchor = false;
    m_mouse_tracking = false;

    std::lock_guard lock(s_mouse_pointer.mutex);
    s_mouse_pointer.engaged[m_player_index] = false;
  }

  void UpdateMouse()
  {
    if (!s_mouse_pointer_enabled.load(std::memory_order_relaxed))
    {
      if (m_mouse_ready)
        StopMouse();
      return;
    }

    if (!m_mouse_ready && !m_mouse_failed)
    {
      m_mouse_ready =
          R_SUCCEEDED(padPointerInitialize(&m_mouse_lifetime, MOUSE_AREA_WIDTH, MOUSE_AREA_HEIGHT));
      m_mouse_failed = !m_mouse_ready;
      if (m_mouse_failed)
        WARN_LOG_FMT(CONTROLLERINTERFACE, "{}: Joy-Con mouse unavailable", GetName());
    }
    if (!m_mouse_ready)
      return;

    const bool left = s_mouse_pointer_left.load(std::memory_order_relaxed);
    const MouseReading mouse = ReadMouse(left);

    const bool was_engaged = m_mouse_engaged;
    if (!mouse.active)
    {
      m_mouse_engaged = false;
      m_mouse_has_anchor = false;
    }
    else
    {
      if (!m_mouse_has_anchor)
      {
        m_mouse_anchor = {mouse.x, mouse.y};
        m_mouse_has_anchor = true;
      }

      const s32 travel_x = mouse.x - m_mouse_anchor[0];
      const s32 travel_y = mouse.y - m_mouse_anchor[1];
      const bool travelled =
          travel_x * travel_x + travel_y * travel_y >= MOUSE_ENGAGE_TRAVEL * MOUSE_ENGAGE_TRAVEL;
      const bool scrolled = mouse.wheel_delta_y != 0;
      const bool clicked = mouse.buttons != 0;
      const auto now = std::chrono::steady_clock::now();

      if (!IsInMouseGrip(left))
      {
        m_mouse_engaged = false;
      }
      else if (travelled || scrolled || (m_mouse_engaged && clicked))
      {
        m_mouse_engaged = true;
        m_mouse_last_active = now;
      }
      else if (m_mouse_engaged && now - m_mouse_last_active >= MOUSE_IDLE_TIMEOUT)
      {
        m_mouse_engaged = false;
      }

      if (travelled || m_mouse_engaged != was_engaged)
        m_mouse_anchor = {mouse.x, mouse.y};
    }

    if (m_mouse_engaged != was_engaged)
    {
      INFO_LOG_FMT(CONTROLLERINTERFACE, "{}: {} Joy-Con mouse {}", GetName(),
                   left ? "left" : "right", m_mouse_engaged ? "engaged" : "released");
    }

    const bool moved = mouse.delta_x != 0 || mouse.delta_y != 0;
    std::lock_guard lock(s_mouse_pointer.mutex);
    s_mouse_pointer.engaged[m_player_index] = m_mouse_engaged;
    if (m_mouse_engaged && (moved || !was_engaged))
    {
      s_mouse_pointer.x = ControlState(mouse.x) / MOUSE_AREA_WIDTH * 2 - 1;
      s_mouse_pointer.y = ControlState(mouse.y) / MOUSE_AREA_HEIGHT * 2 - 1;
      ++s_mouse_pointer.serial;
    }
  }
#endif

  const int m_player_index;
  const HidNpadIdType m_npad_id;

  PadState m_pad{};

  u64 m_buttons = 0;
  std::array<ControlState, 4> m_stick{};
  std::array<ControlState, 2> m_trigger{};
  std::array<ControlState, 3> m_accel{};
  std::array<ControlState, 3> m_gyro{};
  std::array<ControlState, 3> m_left_accel{};
  std::array<ControlState, 3> m_left_gyro{};

  u64 m_sideways_buttons = 0;
  std::array<ControlState, 2> m_sideways_stick{};
  std::array<ControlState, 3> m_sideways_accel{};
  std::array<ControlState, 3> m_sideways_gyro{};

  std::atomic<ControlState> m_rumble_low = 0;
  std::atomic<ControlState> m_rumble_high = 0;
  float m_sent_low = 0;
  float m_sent_high = 0;

  HidNpadIdType m_handle_id = HidNpadIdType_Other;
  HidNpadStyleTag m_handle_style{};
  std::array<HidSixAxisSensorHandle, 2> m_sixaxis{};
  std::array<HidVibrationDeviceHandle, 2> m_vibration{};
  s32 m_sixaxis_count = 0;
  s32 m_vibration_count = 0;
  bool m_vibration_is_gc_erm = false;
  bool m_log_sensor_sample = false;

#ifdef __SWITCH2__
  PadPointer m_mouse_lifetime{};
  HidNmouseHandle m_mouse_handle{};
  u64 m_mouse_sampling_number = 0;
  bool m_mouse_tracking = false;
  bool m_mouse_ready = false;
  bool m_mouse_failed = false;
  bool m_mouse_engaged = false;
  bool m_mouse_has_anchor = false;
  std::array<s32, 2> m_mouse_anchor{};
  std::chrono::steady_clock::time_point m_mouse_last_active{};
#endif
};

// The panel belongs to the console rather than to any player.
class TouchDevice final : public Core::Device
{
public:
  TouchDevice()
  {
    AddInput(new Axis("Touch X-", m_x, -1, false));
    AddInput(new Axis("Touch X+", m_x, 1, false));
    AddInput(new Axis("Touch Y-", m_y, -1, false));
    AddInput(new Axis("Touch Y+", m_y, 1, false));
    AddInput(new Trigger("Touch", m_pressed));
    AddInput(new Trigger("Pointing", m_pointing));
  }

  std::string GetName() const override { return "Touchscreen"; }
  std::string GetSource() const override { return SOURCE; }
  int GetSortPriority() const override { return -MAX_PLAYERS; }

  Core::DeviceRemoval UpdateInput() override
  {
    HidTouchScreenState state{};
    if (hidGetTouchScreenStates(&state, 1) != 1)
      return Core::DeviceRemoval::Keep;

    bool mouse_engaged = false;
#ifdef __SWITCH2__
    {
      std::lock_guard lock(s_mouse_pointer.mutex);
      if (s_mouse_pointer.serial != m_mouse_serial)
      {
        m_mouse_serial = s_mouse_pointer.serial;
        m_x = s_mouse_pointer.x;
        m_y = s_mouse_pointer.y;
      }
      mouse_engaged = std::ranges::any_of(s_mouse_pointer.engaged, std::identity{});
    }
#endif

    m_pressed = state.count > 0;
    m_pointing = m_pressed != 0 || mouse_engaged;

    // Leave cursor position where it is rather than snap back to centre.
    if (state.count > 0)
    {
      m_x = state.touches[0].x / TOUCH_WIDTH * 2 - 1;
      m_y = state.touches[0].y / TOUCH_HEIGHT * 2 - 1;
    }

    return Core::DeviceRemoval::Keep;
  }

private:
  ControlState m_x = 0;
  ControlState m_y = 0;
  ControlState m_pressed = 0;
  ControlState m_pointing = 0;
#ifdef __SWITCH2__
  u64 m_mouse_serial = 0;
#endif
};

class InputBackend final : public ciface::InputBackend
{
public:
  explicit InputBackend(ControllerInterface* controller_interface)
      : ciface::InputBackend(controller_interface)
  {
    hidInitializeTouchScreen();
  }

  void PopulateDevices() override
  {
    // Every slot gets a device whether or not anything is attached to it.
    for (int i = 0; i < MAX_PLAYERS; ++i)
      GetControllerInterface().AddDevice(std::make_shared<NpadDevice>(i));

    GetControllerInterface().AddDevice(std::make_shared<TouchDevice>());
  }
};
}  // namespace

std::unique_ptr<ciface::InputBackend> CreateInputBackend(ControllerInterface* controller_interface)
{
  return std::make_unique<InputBackend>(controller_interface);
}

#ifdef __SWITCH2__
void SetMousePointer(bool enabled, bool left_joycon)
{
  s_mouse_pointer_enabled.store(enabled, std::memory_order_relaxed);
  s_mouse_pointer_left.store(left_joycon, std::memory_order_relaxed);
}
#endif

std::optional<std::string> GetPressedInput(const PadState& pad)
{
  const u64 buttons = padGetButtons(&pad);
  for (const ButtonDef& button : BUTTONS)
  {
    if (buttons & button.mask)
      return button.name;
  }

  for (const auto& [name, index] : {std::pair{"Left", 0}, std::pair{"Right", 1}})
  {
    const HidAnalogStickState stick = padGetStickPos(&pad, index);
    const ControlState x = ControlState(stick.x) / JOYSTICK_MAX;
    const ControlState y = ControlState(stick.y) / JOYSTICK_MAX;
    if (std::abs(x) < DETECT_THRESHOLD && std::abs(y) < DETECT_THRESHOLD)
      continue;

    if (std::abs(x) > std::abs(y))
      return fmt::format("{} X{}", name, x < 0 ? '-' : '+');
    return fmt::format("{} Y{}", name, y < 0 ? '-' : '+');
  }

  return std::nullopt;
}
}  // namespace ciface::Horizon
