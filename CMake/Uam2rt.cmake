# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

set(UAM2RT_INCLUDE_DIR "${NX_PORTLIBS_SWITCH2}/include")
set(UAM2RT_LIBRARY "${NX_PORTLIBS_SWITCH2}/lib/libuam2rt.a")

foreach(_f "${UAM2RT_INCLUDE_DIR}/uam2rt.h" "${UAM2RT_LIBRARY}")
  if(NOT EXISTS "${_f}")
    message(FATAL_ERROR
      "libuam2rt not found at ${_f}.\n"
      "Build uam2 with -Druntime=true and install it into portlibs/switch2.")
  endif()
endforeach()

add_library(uam2rt_library STATIC IMPORTED GLOBAL)
set_target_properties(uam2rt_library PROPERTIES IMPORTED_LOCATION "${UAM2RT_LIBRARY}")

add_library(uam2rt INTERFACE)
target_include_directories(uam2rt INTERFACE "${UAM2RT_INCLUDE_DIR}")
target_link_libraries(uam2rt INTERFACE uam2rt_library stdc++)
