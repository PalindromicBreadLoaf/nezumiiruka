// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#ifdef __SWITCH__

#include <cstddef>

namespace Common::HorizonFastmem
{
constexpr std::size_t ARENA_GUARD_SIZE = 0x200000;
constexpr std::size_t ARENA_SIZE = 2 * 0x1'0000'0000ull + 3 * ARENA_GUARD_SIZE;

void ReserveArenaAddressSpace();

// Whether one allocation can be aliased into several address ranges, which is all the fastmem
// arena needs.
bool IsArenaSupported();

// Whether an alias can then be made read-only, which is what page table fastmem needs to fault on
// writes to a read-only guest page.
bool AreReadOnlyMappingsSupported();
}  // namespace Common::HorizonFastmem

#endif
