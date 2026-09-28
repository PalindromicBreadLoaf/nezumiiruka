# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

# borealis's own CMake bundles a second fmt and builds for every platform it supports. This builds
# only the Switch/deko3d configuration, against Dolphin's fmt.

find_path(GLM_INCLUDE_DIR glm/vec2.hpp)
if(NOT GLM_INCLUDE_DIR)
  message(FATAL_ERROR "glm not found; NanoVG's deko3d renderer needs it. Install it with "
                      "dkp-pacman -S switch-glm.")
endif()

set(_brls "${CMAKE_CURRENT_SOURCE_DIR}/borealis/library")
set(_brls_extern "${_brls}/lib/extern")

file(GLOB_RECURSE _yoga_sources CONFIGURE_DEPENDS "${_brls_extern}/yoga/yoga/*.cpp")
add_library(brls_yoga STATIC ${_yoga_sources})
target_include_directories(brls_yoga PUBLIC "${_brls_extern}/yoga")
target_compile_definitions(brls_yoga PUBLIC YG_ENABLE_EVENTS)
set_target_properties(brls_yoga PROPERTIES CXX_STANDARD 20)
dolphin_disable_warnings(brls_yoga)

add_library(brls_tinyxml2 STATIC "${_brls_extern}/tinyxml2/tinyxml2.cpp")
target_include_directories(brls_tinyxml2 PUBLIC "${_brls}/include/borealis/extern/tinyxml2")
dolphin_disable_warnings(brls_tinyxml2)

file(GLOB _brls_sources CONFIGURE_DEPENDS
  "${_brls}/lib/core/*.cpp"
  "${_brls}/lib/core/touch/*.cpp"
  "${_brls}/lib/views/*.cpp"
  "${_brls}/lib/views/cells/*.cpp"
  "${_brls}/lib/views/widgets/*.cpp"
  "${_brls}/lib/platforms/switch/*.cpp"
  "${_brls_extern}/libretro-common/compat/*.c"
  "${_brls_extern}/libretro-common/encodings/*.c"
  "${_brls_extern}/libretro-common/features/*.c"
  "${_brls_extern}/nanovg/deko3d/*.cpp"
  "${_brls_extern}/nanovg/deko3d/framework/*.cpp"
  "${_brls_extern}/switch-libpulsar/src/*/*.c"
)
# switch_wrapper.c is left out on purpose: it defines userAppInit/userAppExit, which would take
# over process start-up. The shell brings up the services it needs itself.
list(APPEND _brls_sources "${_brls_extern}/nanovg/nanovg.c")
# tinygltf also carries an stb_image implementation, and nanovg.c is the only user of this one.
set_source_files_properties("${_brls_extern}/nanovg/nanovg.c" PROPERTIES
  COMPILE_DEFINITIONS STB_IMAGE_STATIC)

add_library(borealis STATIC ${_brls_sources})
target_include_directories(borealis PUBLIC
  "${_brls}/include"
  "${_brls}/include/borealis/extern"
  "${_brls}/include/borealis/extern/nanovg"
  "${_brls_extern}/tweeny/include"
  "${_brls_extern}/switch-libpulsar/include"
  "${GLM_INCLUDE_DIR}"
)
target_compile_definitions(borealis PUBLIC
  BOREALIS_USE_DEKO3D
  BRLS_RESOURCES="romfs:/"
  HAVE_LIBNX
  SWITCH
  STBI_NO_THREAD_LOCALS
)
target_link_libraries(borealis PUBLIC brls_yoga brls_tinyxml2 fmt::fmt deko3d)
set_target_properties(borealis PROPERTIES CXX_STANDARD 17)
dolphin_disable_warnings(borealis)
# Dolphin builds without exceptions, but borealis's headers catch them (nlohmann::json, its
# logger). Any source that includes them needs BOREALIS_EXCEPTIONS as well.
set(BOREALIS_EXCEPTIONS -fexceptions PARENT_SCOPE)
target_compile_options(borealis PRIVATE -fexceptions)

# Everything borealis reads at runtime, laid out as the NRO's romfs.
set(BOREALIS_ROMFS_DIR "${CMAKE_BINARY_DIR}/romfs" PARENT_SCOPE)
set(_romfs "${CMAKE_BINARY_DIR}/romfs")
set(_romfs_files)

set(_shader_dir "${_brls_extern}/nanovg/deko3d/shaders")
foreach(_shader fill_vsh:vert fill_fsh:frag fill_aa_fsh:frag)
  string(REPLACE ":" ";" _shader "${_shader}")
  list(GET _shader 0 _name)
  list(GET _shader 1 _stage)
  set(_out "${_romfs}/shaders/${_name}.dksh")
  add_custom_command(
    OUTPUT "${_out}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_romfs}/shaders"
    COMMAND "${UAM_HOST_EXECUTABLE}" -s ${_stage} -o "${_out}" "${_shader_dir}/${_name}.glsl"
    DEPENDS uam_host "${_shader_dir}/${_name}.glsl"
    COMMENT "Compiling NanoVG shader ${_name}"
    VERBATIM)
  list(APPEND _romfs_files "${_out}")
endforeach()

foreach(_asset
    material/MaterialIcons-Regular.ttf
    img/sys/cursor.png
    i18n/en-US/hints.json)
  set(_out "${_romfs}/${_asset}")
  add_custom_command(
    OUTPUT "${_out}"
    COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/borealis/resources/${_asset}"
            "${_out}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/borealis/resources/${_asset}"
    VERBATIM)
  list(APPEND _romfs_files "${_out}")
endforeach()

add_custom_target(borealis_romfs_files DEPENDS ${_romfs_files})
set(BOREALIS_ROMFS_FILES ${_romfs_files} PARENT_SCOPE)
