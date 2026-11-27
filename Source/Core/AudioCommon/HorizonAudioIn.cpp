// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "AudioCommon/HorizonAudioIn.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

#include "Common/Logging/Log.h"
#include "Common/Thread.h"

namespace AudioCommon
{
namespace
{
constexpr u64 WAIT_TIMEOUT_NS = 100'000'000;
constexpr std::size_t DEVICE_NAME_LENGTH = 0x100;
constexpr s32 MAX_LISTED_DEVICES = 8;

std::atomic<bool> s_in_use = false;

void LogDevices()
{
  std::array<char, DEVICE_NAME_LENGTH * MAX_LISTED_DEVICES> names{};
  u32 count = 0;
  const Result result = audinListAudioIns(names.data(), MAX_LISTED_DEVICES, &count);
  if (R_FAILED(result))
  {
    WARN_LOG_FMT(AUDIO, "audinListAudioIns failed: {:#010x}", result);
    return;
  }

  for (u32 i = 0; i < std::min<u32>(count, MAX_LISTED_DEVICES); ++i)
  {
    const char* const name = &names[i * DEVICE_NAME_LENGTH];
    NOTICE_LOG_FMT(AUDIO, "Audio input {}: {}", i,
                   std::string_view(name, strnlen(name, DEVICE_NAME_LENGTH)));
  }
}
}  // namespace

HorizonAudioIn::~HorizonAudioIn()
{
  Stop();
}

bool HorizonAudioIn::Start(u32 sample_rate, Callback callback)
{
  Stop();

  if (s_in_use.exchange(true))
  {
    WARN_LOG_FMT(AUDIO, "The microphone is already in use by another emulated device");
    return false;
  }
  m_claimed = true;

  const Result init_result = audinInitialize();
  if (R_FAILED(init_result))
  {
    ERROR_LOG_FMT(AUDIO, "audinInitialize failed: {:#010x}", init_result);
    Stop();
    return false;
  }
  m_audin_open = true;

  LogDevices();

  m_input_rate = audinGetSampleRate();
  m_input_channels = audinGetChannelCount();
  const PcmFormat format = audinGetPcmFormat();
  if (m_input_rate == 0 || m_input_channels == 0 || format != PcmFormat_Int16)
  {
    ERROR_LOG_FMT(AUDIO, "Unsupported audin format: {} Hz, {} channels, PCM format {}",
                  m_input_rate, m_input_channels, static_cast<int>(format));
    Stop();
    return false;
  }

  for (AudioInBuffer& buffer : m_buffers)
  {
    void* const memory = std::aligned_alloc(0x1000, BUFFER_BYTES);
    if (memory == nullptr)
    {
      ERROR_LOG_FMT(AUDIO, "Could not allocate a {} byte microphone buffer", BUFFER_BYTES);
      Stop();
      return false;
    }
    std::memset(memory, 0, BUFFER_BYTES);
    buffer = {.next = nullptr,
              .buffer = memory,
              .buffer_size = BUFFER_BYTES,
              .data_size = BUFFER_BYTES,
              .data_offset = 0};

    const Result append_result = audinAppendAudioInBuffer(&buffer);
    if (R_FAILED(append_result))
    {
      ERROR_LOG_FMT(AUDIO, "audinAppendAudioInBuffer failed: {:#010x}", append_result);
      Stop();
      return false;
    }
  }

  const Result start_result = audinStartAudioIn();
  if (R_FAILED(start_result))
  {
    ERROR_LOG_FMT(AUDIO, "audinStartAudioIn failed: {:#010x}", start_result);
    Stop();
    return false;
  }

  m_callback = std::move(callback);
  m_output_rate = sample_rate;
  m_phase = 0;
  m_sum = 0;
  m_summed = 0;

  NOTICE_LOG_FMT(AUDIO, "Microphone capture started: {} Hz, {} channels, delivered at {} Hz",
                 m_input_rate, m_input_channels, m_output_rate);

  m_running = true;
  m_thread = std::thread(&HorizonAudioIn::CaptureLoop, this);
  return true;
}

void HorizonAudioIn::Stop()
{
  m_running = false;
  if (m_thread.joinable())
    m_thread.join();

  if (m_audin_open)
  {
    audinStopAudioIn();
    audinExit();
    m_audin_open = false;
  }

  for (AudioInBuffer& buffer : m_buffers)
  {
    std::free(buffer.buffer);
    buffer = {};
  }

  m_callback = {};
  if (std::exchange(m_claimed, false))
    s_in_use = false;
}

void HorizonAudioIn::CaptureLoop()
{
  Common::SetCurrentThreadName("Microphone thread - horizon");

  while (m_running)
  {
    AudioInBuffer* released = nullptr;
    u32 released_count = 0;
    if (R_FAILED(audinWaitCaptureFinish(&released, &released_count, WAIT_TIMEOUT_NS)))
      continue;

    while (released != nullptr)
    {
      Deliver(*released);
      audinAppendAudioInBuffer(released);

      released = nullptr;
      if (R_FAILED(audinGetReleasedAudioInBuffer(&released, &released_count)) ||
          released_count == 0)
      {
        break;
      }
    }
  }
}

void HorizonAudioIn::Deliver(const AudioInBuffer& buffer)
{
  const auto* const samples = static_cast<const s16*>(buffer.buffer);
  const std::size_t frames =
      std::min<std::size_t>(buffer.data_size, buffer.buffer_size) / sizeof(s16) / m_input_channels;

  m_output.clear();
  for (std::size_t frame = 0; frame < frames; ++frame)
  {
    s32 mixed = 0;
    for (u32 channel = 0; channel < m_input_channels; ++channel)
      mixed += samples[frame * m_input_channels + channel];
    mixed /= s32(m_input_channels);

    if (m_output_rate >= m_input_rate)
    {
      m_output.push_back(s16(mixed));
      continue;
    }

    m_sum += mixed;
    ++m_summed;
    m_phase += m_output_rate;
    if (m_phase >= m_input_rate)
    {
      m_phase -= m_input_rate;
      m_output.push_back(s16(m_sum / s32(m_summed)));
      m_sum = 0;
      m_summed = 0;
    }
  }

  if (!m_output.empty() && m_callback)
    m_callback(m_output);
}
}  // namespace AudioCommon
