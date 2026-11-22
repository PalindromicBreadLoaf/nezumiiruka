# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

find_package(Git QUIET)

function(lsfg_revision directory out_var)
  set(_revision "unknown")
  if(GIT_FOUND)
    execute_process(
      COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
      WORKING_DIRECTORY "${directory}"
      OUTPUT_VARIABLE _output
      OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET
      RESULT_VARIABLE _result)
    if(_result EQUAL 0 AND _output)
      set(_revision "${_output}")
    endif()
  endif()
  set(${out_var} "${_revision}" PARENT_SCOPE)
endfunction()

set(_spirv_cross_root "${CMAKE_SOURCE_DIR}/Externals/spirv_cross/SPIRV-Cross")

lsfg_revision("${_spirv_cross_root}" LSFG_SPIRV_CROSS_COMMIT)
if(NEZUMIIRUKA_OUNCE)
  file(SHA1 "${UAM2RT_LIBRARY}" _uam2rt_hash)
  set(LSFG_UAM_COMMIT "uam2rt-${_uam2rt_hash}")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${UAM2RT_LIBRARY}")
else()
  lsfg_revision("${UAM_ROOT}" LSFG_UAM_COMMIT)
endif()
configure_file(version.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/include/lsfg/common/version.hpp" @ONLY)

add_library(lsfg_spirv_cross STATIC
  "${_spirv_cross_root}/spirv_cfg.cpp"
  "${_spirv_cross_root}/spirv_cross.cpp"
  "${_spirv_cross_root}/spirv_cross_parsed_ir.cpp"
  "${_spirv_cross_root}/spirv_glsl.cpp"
  "${_spirv_cross_root}/spirv_parser.cpp"
)
target_include_directories(lsfg_spirv_cross SYSTEM PUBLIC "${_spirv_cross_root}")
target_compile_options(lsfg_spirv_cross PRIVATE -w -fexceptions)
set_target_properties(lsfg_spirv_cross PROPERTIES INTERPROCEDURAL_OPTIMIZATION OFF)
target_compile_options(lsfg_spirv_cross PRIVATE -fno-lto)

add_library(lsfg STATIC
  src/binding.cpp
  src/cache_format.cpp
  src/cache_load.cpp
  src/cache_store.cpp
  src/dksh.cpp
  src/error.cpp
  src/image_graph.cpp
  src/layout.cpp
  src/pe_resources.cpp
  src/prepare.cpp
  src/schedule.cpp
  src/sha256.cpp
  src/shader_set.cpp
  src/spirv_module.cpp
  src/translate.cpp
)
target_include_directories(lsfg
PUBLIC
  include
  "${CMAKE_CURRENT_BINARY_DIR}/include"
PRIVATE
  "${CMAKE_SOURCE_DIR}/Source/Core/VideoBackends/Deko3D"
)
set_target_properties(lsfg PROPERTIES CXX_STANDARD 20 CXX_EXTENSIONS OFF)
target_compile_options(lsfg PRIVATE -w)
set_source_files_properties(src/translate.cpp PROPERTIES COMPILE_OPTIONS "-fexceptions;-fno-lto")
target_link_libraries(lsfg PRIVATE lsfg_spirv_cross)

if(NEZUMIIRUKA_OUNCE)
  target_sources(lsfg PRIVATE src/compile_uam2rt.cpp)
  target_compile_definitions(lsfg PUBLIC LSFG_UAM2RT)
  target_link_libraries(lsfg PRIVATE uam2rt glslang::glslang glslang::SPIRV
                        glslang::glslang-default-resource-limits)
else()
  target_sources(lsfg PRIVATE src/compile_uam.cpp)
  target_link_libraries(lsfg PRIVATE uam)
endif()
