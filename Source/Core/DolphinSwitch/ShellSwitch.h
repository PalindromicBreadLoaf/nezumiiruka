// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <string>

#include "Common/CommonTypes.h"

namespace Shell
{
struct BootRequest
{
  std::string path;
  u64 nand_title_id = 0;
  bool riivolution = false;

  bool IsEmpty() const { return path.empty() && nand_title_id == 0; }
};

BootRequest Run(const std::string& notice);
}  // namespace Shell
