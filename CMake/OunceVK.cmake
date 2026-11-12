# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

set(_ouncevk_source "${NX_PORTLIBS_SWITCH2}/lib/libouncevk-gl.a")
if(NOT EXISTS "${_ouncevk_source}")
  message(FATAL_ERROR "OunceVK not found at ${_ouncevk_source}. Install the kaiHEN SDK.")
endif()

set(_ouncevk_dir "${CMAKE_BINARY_DIR}/ouncevk")
set(_ouncevk_library "${_ouncevk_dir}/libouncevk_dolphin.a")
add_custom_command(
  OUTPUT "${_ouncevk_library}"
  COMMAND "${CMAKE_COMMAND}" -E rm -rf "${_ouncevk_dir}/extract"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${_ouncevk_dir}/extract"
  COMMAND "${CMAKE_COMMAND}" -E chdir "${_ouncevk_dir}/extract" "${CMAKE_AR}" x "${_ouncevk_source}"
  COMMAND "${CMAKE_OBJCOPY}"
          --redefine-sym vkGetInstanceProcAddr=ouncevk_vkGetInstanceProcAddr
          "${_ouncevk_dir}/extract/egl.o" "${_ouncevk_dir}/extract/egl_renamed.o"
  COMMAND "${CMAKE_OBJCOPY}" --wildcard --localize-symbol=vk*
          "${_ouncevk_dir}/extract/egl_renamed.o" "${_ouncevk_dir}/egl_dolphin.o"
  COMMAND "${CMAKE_COMMAND}" -E rm -f "${_ouncevk_library}"
  COMMAND "${CMAKE_AR}" rcs "${_ouncevk_library}" "${_ouncevk_dir}/egl_dolphin.o"
  DEPENDS "${_ouncevk_source}"
  COMMENT "Localising OunceVK's Vulkan symbols"
  VERBATIM)
add_custom_target(ouncevk_localised DEPENDS "${_ouncevk_library}")

add_library(ouncevk_driver STATIC IMPORTED GLOBAL)
set_target_properties(ouncevk_driver PROPERTIES IMPORTED_LOCATION "${_ouncevk_library}")
add_dependencies(ouncevk_driver ouncevk_localised)

add_library(ouncevk INTERFACE)
target_link_libraries(ouncevk INTERFACE ouncevk_driver stdc++ m)
