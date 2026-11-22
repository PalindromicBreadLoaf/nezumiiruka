// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "Common/HorizonClocks.h"

#include <algorithm>
#include <array>
#include <mutex>
#include <optional>
#include <vector>

#include <switch.h>

#include <fmt/ranges.h>

#include "Common/CommonTypes.h"
#include "Common/Logging/Log.h"

namespace Common::HorizonClocks
{
namespace
{
std::mutex s_mutex;
std::optional<u32> s_original_configuration;
int s_boost_count = 0;

bool IsApplication()
{
  return appletGetAppletType() == AppletType_Application;
}
}  // namespace

void ApplyPerformanceConfiguration(u32 configuration)
{
  if (!IsApplication())
    return;

#ifdef __SWITCH2__
  // TODO: Switch 2 apm
  return;
#endif

  std::lock_guard lock(s_mutex);
  if (!s_original_configuration)
  {
    u32 original = 0;
    if (R_SUCCEEDED(apmGetPerformanceConfiguration(ApmPerformanceMode_Normal, &original)))
      s_original_configuration = original;
    else
      s_original_configuration = 0;
  }

  const Result rc = apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, configuration);
  if (R_FAILED(rc))
  {
    WARN_LOG_FMT(COMMON, "apmSetPerformanceConfiguration({:#010x}) failed: {:#010x}", configuration,
                 rc);
    return;
  }
  NOTICE_LOG_FMT(COMMON, "Requested performance configuration {:#010x}", configuration);
}

void RestorePerformanceConfiguration()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_original_configuration.value_or(0) != 0)
    apmSetPerformanceConfiguration(ApmPerformanceMode_Normal, *s_original_configuration);
  s_original_configuration.reset();
}

void AcquireCpuBoost()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count++ != 0)
    return;

  const Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_FastLoad);
  if (R_FAILED(rc))
    WARN_LOG_FMT(COMMON, "appletSetCpuBoostMode(FastLoad) failed: {:#010x}", rc);
  else
    NOTICE_LOG_FMT(COMMON, "CPU boost on");
}

void ReleaseCpuBoost()
{
  if (!IsApplication())
    return;

  std::lock_guard lock(s_mutex);
  if (s_boost_count == 0 || --s_boost_count != 0)
    return;

  const Result rc = appletSetCpuBoostMode(ApmCpuBoostMode_Normal);
  if (R_FAILED(rc))
    WARN_LOG_FMT(COMMON, "appletSetCpuBoostMode(Normal) failed: {:#010x}", rc);
  else
    NOTICE_LOG_FMT(COMMON, "CPU boost off");
}

#ifdef __SWITCH2__
namespace
{
constexpr u64 MAINTAIN_INTERVAL_NS = 1'000'000'000;
constexpr u32 RANGE_STEP_HZ = 100'000'000;
constexpr size_t MAX_RATES = 128;

constexpr u32 RGLTR_CPU_DOMAIN = 0x3A000003;
constexpr u32 RGLTR_GPU_DOMAIN = 0x3A000004;

constexpr size_t DOMAIN_COUNT = static_cast<size_t>(ClockDomain::Count);
constexpr std::array<PcvModuleId, DOMAIN_COUNT> MODULE_IDS = {PcvModuleId_CpuBus, PcvModuleId_GPU,
                                                              PcvModuleId_EMC};
constexpr std::array<const char*, DOMAIN_COUNT> DOMAIN_NAMES = {"CPU", "GPU", "memory"};

struct DomainState
{
  ClkrstSession session{};
  bool open = false;
  std::optional<std::vector<u32>> rates;
  u32 override_hz = 0;
  std::optional<u32> original_hz;
};

struct ClockState
{
  bool clkrst_ready = false;
  bool clkrst_failed = false;
  std::array<DomainState, DOMAIN_COUNT> domains;

  bool sensors_tried = false;
  bool ts_ready = false;
  TsSession soc_session{};
  TsSession pcb_session{};
  bool soc_open = false;
  bool pcb_open = false;
  Service rgltr{};
  bool rgltr_ready = false;
  Service cpu_regulator{};
  Service gpu_regulator{};
  bool cpu_regulator_open = false;
  bool gpu_regulator_open = false;

  u64 last_maintain_tick = 0;
  std::optional<Telemetry> telemetry;
};

std::mutex s_clock_mutex;
ClockState s_clocks;

DomainState* OpenDomain(ClockDomain domain)
{
  if (!s_clocks.clkrst_ready)
  {
    if (s_clocks.clkrst_failed)
      return nullptr;
    const Result rc = clkrstInitialize();
    if (R_FAILED(rc))
    {
      WARN_LOG_FMT(COMMON, "clkrstInitialize failed: {:#010x}", rc);
      s_clocks.clkrst_failed = true;
      return nullptr;
    }
    s_clocks.clkrst_ready = true;
  }

  const size_t index = static_cast<size_t>(domain);
  DomainState& state = s_clocks.domains[index];
  if (!state.open)
  {
    const Result rc = clkrstOpenSession(&state.session, MODULE_IDS[index], 3);
    if (R_FAILED(rc))
    {
      WARN_LOG_FMT(COMMON, "clkrstOpenSession({}) failed: {:#010x}", DOMAIN_NAMES[index], rc);
      return nullptr;
    }
    state.open = true;
  }
  return &state;
}

std::optional<u32> ReadRate(DomainState& state)
{
  u32 hz = 0;
  if (R_FAILED(clkrstGetClockRate(&state.session, &hz)))
    return std::nullopt;
  return hz;
}

Result WriteRate(ClockDomain domain, DomainState& state, u32 hz)
{
  const Result rc = clkrstSetClockRate(&state.session, hz);
  if (R_FAILED(rc))
  {
    WARN_LOG_FMT(COMMON, "clkrstSetClockRate({}, {}) failed: {:#010x}",
                 DOMAIN_NAMES[static_cast<size_t>(domain)], hz, rc);
  }
  return rc;
}

void OpenSensors()
{
  if (s_clocks.sensors_tried)
    return;
  s_clocks.sensors_tried = true;

  if (R_SUCCEEDED(tsInitialize()))
  {
    s_clocks.ts_ready = true;
    s_clocks.soc_open =
        R_SUCCEEDED(tsOpenSession(&s_clocks.soc_session, TsDeviceCode_LocationExternal));
    s_clocks.pcb_open =
        R_SUCCEEDED(tsOpenSession(&s_clocks.pcb_session, TsDeviceCode_LocationInternal));
  }

  if (R_SUCCEEDED(smGetService(&s_clocks.rgltr, "rgltr")))
  {
    s_clocks.rgltr_ready = true;
    s_clocks.cpu_regulator_open =
        R_SUCCEEDED(serviceDispatchIn(&s_clocks.rgltr, 0, RGLTR_CPU_DOMAIN, .out_num_objects = 1,
                                      .out_objects = &s_clocks.cpu_regulator));
    s_clocks.gpu_regulator_open =
        R_SUCCEEDED(serviceDispatchIn(&s_clocks.rgltr, 0, RGLTR_GPU_DOMAIN, .out_num_objects = 1,
                                      .out_objects = &s_clocks.gpu_regulator));
  }

  NOTICE_LOG_FMT(COMMON, "Sensors: SoC {}, PCB {}, CPU regulator {}, GPU regulator {}",
                 s_clocks.soc_open, s_clocks.pcb_open, s_clocks.cpu_regulator_open,
                 s_clocks.gpu_regulator_open);
}

std::optional<u32> ReadMicrovolts(Service& regulator, bool open)
{
  u32 microvolts = 0;
  if (!open || R_FAILED(serviceDispatchOut(&regulator, 4, microvolts)))
    return std::nullopt;
  return microvolts;
}

std::optional<float> ReadCelsius(TsSession& session, bool open)
{
  float celsius = 0.0f;
  if (!open || R_FAILED(tsSessionGetTemperature(&session, &celsius)))
    return std::nullopt;
  return celsius;
}

u32 ReadRateOrZero(ClockDomain domain)
{
  DomainState* const state = OpenDomain(domain);
  return state ? ReadRate(*state).value_or(0) : 0;
}

void RefreshTelemetry()
{
  OpenSensors();

  Telemetry telemetry;
  telemetry.cpu_hz = ReadRateOrZero(ClockDomain::Cpu);
  telemetry.gpu_hz = ReadRateOrZero(ClockDomain::Gpu);
  telemetry.memory_hz = ReadRateOrZero(ClockDomain::Memory);
  telemetry.cpu_microvolts = ReadMicrovolts(s_clocks.cpu_regulator, s_clocks.cpu_regulator_open);
  telemetry.gpu_microvolts = ReadMicrovolts(s_clocks.gpu_regulator, s_clocks.gpu_regulator_open);
  telemetry.soc_celsius = ReadCelsius(s_clocks.soc_session, s_clocks.soc_open);
  telemetry.pcb_celsius = ReadCelsius(s_clocks.pcb_session, s_clocks.pcb_open);
  s_clocks.telemetry = telemetry;
}
}  // namespace

std::vector<u32> GetClockRates(ClockDomain domain)
{
  std::lock_guard lock(s_clock_mutex);
  DomainState* const state = OpenDomain(domain);
  if (!state)
    return {};
  if (state->rates)
    return *state->rates;

  std::vector<u32> rates(MAX_RATES);
  PcvClockRatesListType type = PcvClockRatesListType_Invalid;
  s32 count = 0;
  const Result rc = clkrstGetPossibleClockRates(&state->session, rates.data(),
                                                static_cast<s32>(rates.size()), &type, &count);
  if (R_FAILED(rc) || count <= 0)
  {
    WARN_LOG_FMT(COMMON, "clkrstGetPossibleClockRates({}) failed: {:#010x}",
                 DOMAIN_NAMES[static_cast<size_t>(domain)], rc);
    state->rates.emplace();
    return {};
  }
  rates.resize(std::min<size_t>(count, MAX_RATES));

  if (type == PcvClockRatesListType_Range && rates.size() == 2 && rates[1] > rates[0])
  {
    const u32 min_hz = rates[0];
    const u32 max_hz = rates[1];
    rates.clear();
    for (u64 hz = min_hz; hz < max_hz && rates.size() < MAX_RATES - 1; hz += RANGE_STEP_HZ)
      rates.push_back(static_cast<u32>(hz));
    rates.push_back(max_hz);
  }

  NOTICE_LOG_FMT(COMMON, "{} clock rates ({}): {}", DOMAIN_NAMES[static_cast<size_t>(domain)],
                 type == PcvClockRatesListType_Range ? "range" : "discrete",
                 fmt::join(rates, ", "));
  state->rates = rates;
  return rates;
}

void SetClockOverride(ClockDomain domain, u32 hz)
{
  std::lock_guard lock(s_clock_mutex);
  if (hz == 0 && s_clocks.domains[static_cast<size_t>(domain)].override_hz == 0)
    return;

  DomainState* const state = OpenDomain(domain);
  if (!state)
    return;

  if (hz == 0)
  {
    if (state->override_hz != 0 && state->original_hz)
      WriteRate(domain, *state, *state->original_hz);
    state->override_hz = 0;
    state->original_hz.reset();
    return;
  }

  if (!state->original_hz)
    state->original_hz = ReadRate(*state);
  if (R_SUCCEEDED(WriteRate(domain, *state, hz)))
  {
    NOTICE_LOG_FMT(COMMON, "{} clock override {} Hz", DOMAIN_NAMES[static_cast<size_t>(domain)],
                   hz);
  }
  state->override_hz = hz;
}

void ClearClockOverrides()
{
  for (size_t i = 0; i < DOMAIN_COUNT; ++i)
    SetClockOverride(static_cast<ClockDomain>(i), 0);
}

void MaintainClocks()
{
  std::lock_guard lock(s_clock_mutex);
  const u64 now = armTicksToNs(armGetSystemTick());
  if (s_clocks.last_maintain_tick != 0 && now - s_clocks.last_maintain_tick < MAINTAIN_INTERVAL_NS)
    return;
  s_clocks.last_maintain_tick = now;

  for (size_t i = 0; i < DOMAIN_COUNT; ++i)
  {
    DomainState& state = s_clocks.domains[i];
    if (!state.open || state.override_hz == 0)
      continue;
    const std::optional<u32> current = ReadRate(state);
    if (current && *current != state.override_hz)
      WriteRate(static_cast<ClockDomain>(i), state, state.override_hz);
  }

  RefreshTelemetry();
}

std::optional<Telemetry> GetTelemetry()
{
  std::lock_guard lock(s_clock_mutex);
  return s_clocks.telemetry;
}

void ShutdownClocks()
{
  ClearClockOverrides();

  std::lock_guard lock(s_clock_mutex);
  for (DomainState& state : s_clocks.domains)
  {
    if (state.open)
      clkrstCloseSession(&state.session);
    state.open = false;
  }
  if (s_clocks.clkrst_ready)
    clkrstExit();

  if (s_clocks.soc_open)
    tsSessionClose(&s_clocks.soc_session);
  if (s_clocks.pcb_open)
    tsSessionClose(&s_clocks.pcb_session);
  if (s_clocks.ts_ready)
    tsExit();

  if (s_clocks.cpu_regulator_open)
    serviceClose(&s_clocks.cpu_regulator);
  if (s_clocks.gpu_regulator_open)
    serviceClose(&s_clocks.gpu_regulator);
  if (s_clocks.rgltr_ready)
    serviceClose(&s_clocks.rgltr);

  s_clocks = {};
}
#endif
}  // namespace Common::HorizonClocks
