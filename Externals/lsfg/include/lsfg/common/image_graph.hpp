// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Every intermediate image the chain needs, its size and format,
// and every dispatch with the images it reads and writes.
namespace lsfg::graph {

// Bump when the same configuration would produce a differently shaped graph.
inline constexpr std::uint32_t graph_version = 1;

// More would need more presentable images than a swapchain is likely to hold.
inline constexpr std::uint32_t max_generated_frames = 3;

enum class Format : std::uint8_t {
    rgba8 = 0,
    r8 = 1,
    rgba16f = 2,
};

enum class ExtentBase : std::uint8_t {
    output = 0,  // the extent frames are presented at
    flow = 1,    // the extent the motion pyramid is built at
    fixed = 2,   // the descriptor carries its own size
};

enum class ImageRole : std::uint8_t {
    internal = 0,   // owned by the backend for the lifetime of a resolution
    history = 1,    // a retained real frame
    generated = 2,  // where a generated frame is written
    constant = 3,   // cleared once at startup
};

// Halving rounds up, shifting rounds down, and the chain uses both, so the
// order is part of the description:
//   ceil_halve^ceil_halvings(base >> shift_before) >> shift_after
struct alignas(4) ImageDesc {
    std::uint8_t base{};
    std::uint8_t shift_before{};
    std::uint8_t ceil_halvings{};
    std::uint8_t shift_after{};

    std::uint16_t fixed_width{};
    std::uint16_t fixed_height{};

    std::uint8_t format{};
    std::uint8_t role{};
    std::uint16_t reserved0_{};
};

static_assert(sizeof(ImageDesc) == 12);

enum class Sampler : std::uint8_t {
    border_black = 0,  // clamp to a black border
    border_white = 1,  // clamp to a white border
    edge = 2,          // clamp to the edge, comparison always passing
};

inline constexpr std::uint8_t no_uniform_buffer = 0xFFU;
inline constexpr std::size_t max_dispatch_samplers = 2;

// One descriptor set: the images bound to one dispatch of one module. Textures
// come first in the binding list, then storage images, each in the order the
// module declares them.
struct alignas(4) VariantEntry {
    std::uint32_t binding_first{};
    std::uint16_t texture_count{};
    std::uint16_t storage_count{};

    std::array<std::uint8_t, max_dispatch_samplers> samplers{};
    std::uint8_t sampler_count{};
    std::uint8_t uniform_buffer{no_uniform_buffer};
};

static_assert(sizeof(VariantEntry) == 12);

// A dispatch alternates between its variants by real frame index, which is how
// the chain reaches the right history slot without rebuilding descriptors.
struct alignas(4) DispatchEntry {
    std::uint32_t slot{};        // index into shaders::chain_slots()
    std::uint32_t grid_image{};  // the dispatch covers this image
    std::uint32_t variant_first{};

    std::uint16_t variant_count{};
    // Pixels one workgroup covers, as a power of two. Not always the workgroup
    // size: two modules write a mip pyramid and cover more than they are wide.
    std::uint8_t grid_shift{};
    std::uint8_t stage{};  // 0 for the shared prepass, 1 + n for generated frame n
};

static_assert(sizeof(DispatchEntry) == 16);

inline constexpr std::uint8_t prepass_stage = 0;

struct Config {
    bool performance{};
    bool hdr{};
    std::uint32_t generated_frames{1};
    // The motion pyramid may be built below the output extent.
    std::uint32_t flow_numerator{1};
    std::uint32_t flow_denominator{1};
};

// Images 0 and 1 hold consecutive real frames and images 2 onwards are the
// generated-frame destinations.
inline constexpr std::uint32_t history_image_count = 2;

struct Graph {
    Config config;

    std::vector<ImageDesc> images;
    std::vector<DispatchEntry> dispatches;
    std::vector<VariantEntry> variants;
    std::vector<std::uint32_t> bindings;

    std::uint32_t uniform_buffer_count{};
};

[[nodiscard]] ErrorCode build(const Config& config, Graph& out);

inline constexpr std::uint32_t invalid_block_index = 0xFFFF'FFFFU;

// Which module a dispatch runs, as a position inside a precision block.
[[nodiscard]] std::uint32_t block_index_of(const Graph& graph, const DispatchEntry& dispatch) noexcept;

// Rejects a graph whose indices do not point at anything, whose images are not
// laid out as documented above, or whose dispatch tiles cannot be squared with
// the workgroup sizes the modules declare.
[[nodiscard]] ErrorCode validate(const Graph& graph) noexcept;

struct Extent {
    std::uint32_t width{};
    std::uint32_t height{};
};

[[nodiscard]] Extent flow_extent(const Config& config, Extent output) noexcept;
[[nodiscard]] Extent evaluate(const ImageDesc& desc, Extent output, Extent flow) noexcept;

[[nodiscard]] std::uint32_t bytes_per_pixel(Format format) noexcept;

// What the backend has to allocate itself, so the imported frames and the
// destinations are not counted.
[[nodiscard]] std::uint64_t owned_memory_bytes(const Graph& graph, Extent output) noexcept;

// What one module declares, as parsed from its SPIR-V.
struct ModuleInterface {
    std::uint32_t block_index{};
    std::uint32_t textures{};
    std::uint32_t storage_images{};
    std::uint32_t samplers{};
    std::uint32_t uniform_buffers{};
};

// Every dispatch has to bind exactly what its module declares.
[[nodiscard]] ErrorCode check_against_modules(
    const Graph& graph,
    std::span<const ModuleInterface> modules);

// The uniform block every pass that takes one expects. Field order is the
// shaders' own and cannot be rearranged.
struct alignas(4) ConstantBuffer {
    std::array<std::uint32_t, 2> input_offset{};
    std::uint32_t first_iteration{};
    std::uint32_t first_iteration_s{};
    std::uint32_t advanced_color_kind{};
    std::uint32_t hdr_support{};
    float resolution_inverse_scale{};
    float timestamp{};
    float ui_threshold{};
    std::array<std::uint32_t, 3> padding_{};
};

static_assert(sizeof(ConstantBuffer) == 48);

[[nodiscard]] ConstantBuffer constant_buffer(
    std::uint32_t index,
    const Config& config) noexcept;

[[nodiscard]] std::string_view format_name(Format format) noexcept;
[[nodiscard]] std::string_view role_name(ImageRole role) noexcept;
[[nodiscard]] std::string_view sampler_name(Sampler sampler) noexcept;

} // namespace lsfg::graph
