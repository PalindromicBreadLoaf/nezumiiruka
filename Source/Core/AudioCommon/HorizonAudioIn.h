// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <span>
#include <thread>
#include <vector>

#include <switch.h>

#include "Common/CommonTypes.h"

namespace AudioCommon
{
class HorizonAudioIn
{
public:
  using Callback = std::function<void(std::span<const s16> samples)>;

  HorizonAudioIn() = default;
  ~HorizonAudioIn();

  HorizonAudioIn(const HorizonAudioIn&) = delete;
  HorizonAudioIn& operator=(const HorizonAudioIn&) = delete;

  bool Start(u32 sample_rate, Callback callback);
  void Stop();

private:
  static constexpr std::size_t BUFFER_BYTES = 0x1000;
  static constexpr std::size_t BUFFER_COUNT = 6;

  void CaptureLoop();
  void Deliver(const AudioInBuffer& buffer);

  std::array<AudioInBuffer, BUFFER_COUNT> m_buffers{};
  std::thread m_thread;
  std::atomic<bool> m_running = false;
  bool m_claimed = false;
  bool m_audin_open = false;

  Callback m_callback;
  u32 m_input_rate = 0;
  u32 m_input_channels = 0;
  u32 m_output_rate = 0;
  u32 m_phase = 0;
  s32 m_sum = 0;
  u32 m_summed = 0;
  std::vector<s16> m_output;
};
}  // namespace AudioCommon
