// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <lsfg/common/error.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lsfg::pe {

// The only resource type this project reads.
inline constexpr std::uint32_t resource_type_rcdata = 10;

// A directory deeper than type/name/language is not something the PE format
// produces.
inline constexpr std::uint32_t max_directory_depth = 3;
inline constexpr std::size_t max_resources = 8192;

struct Resource {
    std::uint32_t type{};
    std::uint32_t id{};
    std::uint32_t language{};
    std::uint32_t offset{};
    std::uint32_t size{};
};

struct ResourceTable {
    std::vector<Resource> resources;
    // Entries keyed by a name string are skipped.
    std::uint32_t named_entries_skipped{};
};

// Walks a PE32 or PE32+ resource directory.
[[nodiscard]] ErrorCode enumerate_resources(std::span<const std::uint8_t> image, ResourceTable& out);

// Empty when the resource does not lie entirely inside the image.
[[nodiscard]] std::span<const std::uint8_t> resource_data(
    std::span<const std::uint8_t> image,
    const Resource& resource) noexcept;

} // namespace lsfg::pe
