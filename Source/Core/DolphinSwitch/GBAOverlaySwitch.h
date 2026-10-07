// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <memory>

class GBAHostInterface;

namespace HW::GBA
{
class Core;
}

namespace GBAOverlay
{
std::unique_ptr<GBAHostInterface> CreateHost(std::weak_ptr<HW::GBA::Core> core);

void Draw();

void ReleaseTextures();
}  // namespace GBAOverlay
