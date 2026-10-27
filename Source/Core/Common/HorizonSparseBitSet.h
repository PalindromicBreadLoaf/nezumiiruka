// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "Common/Align.h"
#include "Common/CommonTypes.h"

namespace Common
{
// Horizon cannot commit pages on first touch, so a LazyMemoryRegion is a plain heap allocation
// there. That is fine for small sets, but the JIT keeps three bit sets over every instruction
// address (128 MiB each) of which only a handful of bits are ever set.
class HorizonSparseBitSet final
{
public:
  explicit HorizonSparseBitSet(size_t bits) : m_pages(DivideRoundingUp(bits, PAGE_BITS)) {}

  bool IsValid() const { return true; }

  void Clear()
  {
    for (auto& page : m_pages)
      page.reset();
  }

  bool TestBit(size_t bit) const
  {
    const auto& page = m_pages[bit / PAGE_BITS];
    return page && (page[bit % PAGE_BITS / 32] & (1u << (bit % 32))) != 0;
  }

  void SetBit(size_t bit)
  {
    auto& page = m_pages[bit / PAGE_BITS];
    if (!page)
      page = std::make_unique<u32[]>(PAGE_WORDS);
    page[bit % PAGE_BITS / 32] |= 1u << (bit % 32);
  }

  void ClearBits(size_t bits_start, size_t bits_end)
  {
    while (bits_start < bits_end)
    {
      const size_t page_index = bits_start / PAGE_BITS;
      const size_t page_end = (page_index + 1) * PAGE_BITS;
      const size_t range_end = std::min(bits_end, page_end);
      if (auto& page = m_pages[page_index])
      {
        if (bits_start % PAGE_BITS == 0 && range_end == page_end)
        {
          page.reset();
        }
        else
        {
          for (size_t bit = bits_start; bit < range_end; ++bit)
            page[bit % PAGE_BITS / 32] &= ~(1u << (bit % 32));
        }
      }
      bits_start = range_end;
    }
  }

private:
  static constexpr size_t PAGE_BITS = 0x1000 * 8;
  static constexpr size_t PAGE_WORDS = PAGE_BITS / 32;

  std::vector<std::unique_ptr<u32[]>> m_pages;
};

}  // namespace Common
