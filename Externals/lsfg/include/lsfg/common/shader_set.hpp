// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/pe_resources.hpp>

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace lsfg::shaders {

enum class Precision : std::uint8_t {
    low,   // declares Float16
    high,  // does not
};

// The shaders ship as two consecutive blocks of the same size, one per
// precision.
struct ShaderSet {
    std::uint32_t first_resource_id{};
    std::uint32_t block_size{};
    std::uint32_t low_precision_block{};
    std::uint32_t high_precision_block{};
};

// Position of the quality/performance pair of a shader that has both. Applying
// it to a shader that has no performance variant addresses an unrelated one.
inline constexpr std::uint32_t performance_offset = 23;

// Every module in the set uses descriptor set 0 with one fixed range per kind,
// dense from its base, so a binding says both what a resource is and where it
// sits among the ones of its kind.
inline constexpr std::uint32_t binding_range = 16;
inline constexpr std::uint32_t binding_base_uniform_buffer = 0;
inline constexpr std::uint32_t binding_base_sampler = 16;
inline constexpr std::uint32_t binding_base_image = 32;
inline constexpr std::uint32_t binding_base_storage_image = 48;

// The block indices the interpolation chain dispatches. Several chain slots
// share a module; the names follow the stages the chain runs in order.
struct ChainSlot {
    std::string_view name;
    std::uint32_t block_index{};
    bool has_performance_variant{};
};

struct ModuleRequest {
    std::string_view name;  // the first chain slot that uses the module
    std::uint32_t block_index{};
    std::uint32_t resource_id{};
};

[[nodiscard]] std::span<const ChainSlot> chain_slots() noexcept;

// Establishes the block layout from the resources themselves rather than from
// a hard-coded resource ID range, and refuses a DLL whose shaders are not the
// contiguous two-block set this project knows how to read.
[[nodiscard]] ErrorCode identify(
    std::span<const std::uint8_t> image,
    std::span<const pe::Resource> resources,
    ShaderSet& out);

[[nodiscard]] std::uint32_t resource_id_for(
    const ShaderSet& set,
    Precision precision,
    std::uint32_t block_index) noexcept;

// The distinct modules the chain needs, in block order, with duplicates from
// shared chain slots removed.
[[nodiscard]] ErrorCode required_modules(
    const ShaderSet& set,
    Precision precision,
    bool performance,
    std::vector<ModuleRequest>& out);

[[nodiscard]] std::string_view precision_name(Precision precision) noexcept;

} // namespace lsfg::shaders
