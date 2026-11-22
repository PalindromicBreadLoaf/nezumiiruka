# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

if(NEZUMIIRUKA_OUNCE)
  set(DEKO3D_NAME drako3d)
  set(DEKO3D_ROOT "${DEVKITPRO}/libounce")
  set(DEKO3D_INSTALL_HINT "Install drako3d.")
else()
  set(DEKO3D_NAME deko3d)
  set(DEKO3D_ROOT "${DEVKITPRO}/libnx")
  set(DEKO3D_INSTALL_HINT "Install it with dkp-pacman -S switch-tools deko3d.")
endif()

set(DEKO3D_INCLUDE_DIR "${DEKO3D_ROOT}/include")
set(DEKO3D_LIB_DIR "${DEKO3D_ROOT}/lib")
set(DEKO3D_DEBUG_LIBRARY "${DEKO3D_LIB_DIR}/lib${DEKO3D_NAME}d.a")
set(DEKO3D_RELEASE_LIBRARY "${DEKO3D_LIB_DIR}/lib${DEKO3D_NAME}.a")

set(_deko3d_missing "")
foreach(_f
        "${DEKO3D_INCLUDE_DIR}/${DEKO3D_NAME}.h"
        "${DEKO3D_INCLUDE_DIR}/${DEKO3D_NAME}.hpp"
        "${DEKO3D_DEBUG_LIBRARY}"
        "${DEKO3D_RELEASE_LIBRARY}")
  if(NOT EXISTS "${_f}")
    list(APPEND _deko3d_missing "${_f}")
  endif()
endforeach()

if(_deko3d_missing)
  string(REPLACE ";" "\n  " _deko3d_missing_pretty "${_deko3d_missing}")
  message(FATAL_ERROR "${DEKO3D_NAME} not found:\n  ${_deko3d_missing_pretty}\n${DEKO3D_INSTALL_HINT}")
endif()

option(NEZUMIIRUKA_DEKO3D_DEBUG "Link the debug build of deko3d (or drako3d on Switch 2)" OFF)

add_library(deko3d INTERFACE)
target_include_directories(deko3d INTERFACE "${DEKO3D_INCLUDE_DIR}")
if(NEZUMIIRUKA_DEKO3D_DEBUG)
  target_link_libraries(deko3d INTERFACE "${DEKO3D_DEBUG_LIBRARY}")
else()
  target_link_libraries(deko3d INTERFACE
    "$<$<CONFIG:Debug>:${DEKO3D_DEBUG_LIBRARY}>"
    "$<$<NOT:$<CONFIG:Debug>>:${DEKO3D_RELEASE_LIBRARY}>"
  )
endif()
target_compile_definitions(deko3d INTERFACE DK_HPP_SUPPORT_VECTOR)

if(NEZUMIIRUKA_OUNCE)
  set(_deko3d_shim "${CMAKE_BINARY_DIR}/deko3d-shim")
  file(CONFIGURE OUTPUT "${_deko3d_shim}/deko3d.h" CONTENT "#pragma once\n#include <drako3d.h>\n")
  file(CONFIGURE OUTPUT "${_deko3d_shim}/deko3d.hpp" CONTENT "#pragma once\n#include <drako3d.hpp>\n")
  target_include_directories(deko3d BEFORE INTERFACE "${_deko3d_shim}")
endif()
