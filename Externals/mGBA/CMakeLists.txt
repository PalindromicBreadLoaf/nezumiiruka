set(LIBMGBA_ONLY ON)
set(USE_LZMA ON)
add_subdirectory(mgba EXCLUDE_FROM_ALL)
dolphin_disable_warnings(mgba)

target_compile_definitions(mgba PUBLIC HAVE_CRC32 ENABLE_VFS ENABLE_DIRECTORIES)
target_link_libraries(mgba ZLIB::ZLIB)

if(NOT MSVC)
  target_compile_options(mgba PRIVATE -Wno-unused-parameter -Wno-unused-result -Wno-unused-variable)
endif()

if(ANDROID)
  target_compile_definitions(mgba PUBLIC ENABLE_VFS_FD)
  target_compile_definitions(mgba PRIVATE -Dfutimes=futimens)
endif()

if(NEZUMIIRUKA)
  target_sources(mgba PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mgba/src/util/vfs/vfs-fd.c
    ${CMAKE_CURRENT_SOURCE_DIR}/mgba/src/util/vfs/vfs-dirent.c
  )
  target_compile_definitions(mgba PUBLIC ENABLE_VFS_FD)
endif()

add_library(mGBA::mgba ALIAS mgba)
