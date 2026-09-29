// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>
#include <lsfg/common/spirv_module.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace lsfg::translate {

// Where a resource ends up once the module is desktop GLSL.
enum class SlotKind : std::uint8_t {
    uniform_buffer,
    texture,  // a combined image and a sampler
    storage_image,
};

struct SlotAssignment {
    SlotKind kind{};
    std::uint32_t slot{};
    std::uint32_t spirv_binding{};
    std::uint32_t spirv_sampler_binding{};
    bool uses_dummy_sampler{};
    std::string name;
};

// The GLSL compiler's ceilings.
struct Limits {
    std::uint32_t textures{32};
    std::uint32_t storage_images{8};
    std::uint32_t uniform_buffers{16};
    std::uint32_t storage_buffers{16};
};

inline constexpr Limits uam_limits{};

struct Options {
    std::uint32_t glsl_version{450};
    // Desktop GLSL has no unformatted storage image.
    spirv::ImageFormat unformatted_storage_image_format{spirv::ImageFormat::rgba8};
    Limits limits{uam_limits};
};

struct Module {
    std::string glsl;
    std::vector<SlotAssignment> slots;

    std::uint32_t local_size_x{};
    std::uint32_t local_size_y{};
    std::uint32_t local_size_z{};

    std::uint32_t texture_count{};
    std::uint32_t storage_image_count{};
    std::uint32_t uniform_buffer_count{};

    spirv::PatchSummary patch;
    // A separate image used without any sampler, which desktop GLSL cannot
    // express, so one sampler is introduced to combine it with.
    bool needed_dummy_sampler{};
};

// Turns one extracted SPIR-V compute module into GLSL and records where it
// was placed.
[[nodiscard]] ErrorCode to_glsl(
    std::span<const std::uint8_t> module_bytes,
    const Options& options,
    Module& out);

[[nodiscard]] std::string_view slot_kind_name(SlotKind kind) noexcept;

} // namespace lsfg::translate
