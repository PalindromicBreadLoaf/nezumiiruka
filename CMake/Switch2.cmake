# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

# Toolchain for Switch 2 builds against libounce.

if(NOT DEFINED ENV{DEVKITPRO} AND NOT DEVKITPRO)
  message(FATAL_ERROR "Set DEVKITPRO in the environment before configuring.")
endif()

include($ENV{DEVKITPRO}/cmake/Switch.cmake)

set(NEZUMIIRUKA_OUNCE TRUE)

set(NX_ROOT ${DEVKITPRO}/libounce)
set(NX_PORTLIBS_SWITCH2 ${DEVKITPRO}/portlibs/switch2)

list(REMOVE_ITEM ${DKP_PREFIX_VAR} ${DEVKITPRO}/libnx)
__dkp_platform_prefix(${NX_PORTLIBS_SWITCH2} ${NX_ROOT})

find_file(NX_DEFAULT_ICON NAMES default_icon.jpg HINTS "${NX_ROOT}" NO_CMAKE_FIND_ROOT_PATH)
