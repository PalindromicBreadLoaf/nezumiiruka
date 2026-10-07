add_executable(nezumiiruka
  CheatsSwitch.cpp
  CheatsSwitch.h
  ControllerProfilesSwitch.cpp
  ControllerProfilesSwitch.h
  CrashHandlerSwitch.cpp
  HorizonCompatSwitch.cpp
  HorizonExceptionEntry.h
  HorizonExceptionEntry.S
  HostSwitch.cpp
  MainSwitch.cpp
  MallocSwitch.c
  MemoryCardsSwitch.cpp
  MemoryCardsSwitch.h
  PauseMenuSwitch.cpp
  PauseMenuSwitch.h
  PerformanceOverlaySwitch.cpp
  PlatformSwitch.cpp
  RiivolutionSwitch.cpp
  RiivolutionSwitch.h
  SettingsSwitch.cpp
  SettingsSwitch.h
  ShellCheatsSwitch.cpp
  ShellCheatsSwitch.h
  ShellControlsSwitch.cpp
  ShellControlsSwitch.h
  ShellFilePickerSwitch.cpp
  ShellFilePickerSwitch.h
  ShellFormatSwitch.cpp
  ShellFormatSwitch.h
  ShellGameListSwitch.cpp
  ShellGameListSwitch.h
  ShellLibrarySwitch.cpp
  ShellLibrarySwitch.h
  ShellMemoryCardsSwitch.cpp
  ShellMemoryCardsSwitch.h
  ShellSettingsSwitch.cpp
  ShellSettingsSwitch.h
  ShellSwitch.cpp
  ShellSwitch.h
  ShellSystemSwitch.cpp
  ShellSystemSwitch.h
  ShellUpdaterSwitch.cpp
  ShellUpdaterSwitch.h
  UpdaterSwitch.cpp
  UpdaterSwitch.h
  UsbStorageSwitch.cpp
  UsbStorageSwitch.h
)

target_compile_definitions(nezumiiruka PRIVATE NEZUMIIRUKA_VERSION="${NEZUMIIRUKA_VERSION}")

if(USE_MGBA)
  target_sources(nezumiiruka PRIVATE
    GBAOverlaySwitch.cpp
    GBAOverlaySwitch.h
  )
endif()

if(USE_RETRO_ACHIEVEMENTS)
  target_sources(nezumiiruka PRIVATE
    ShellAchievementsSwitch.cpp
    ShellAchievementsSwitch.h
  )
endif()

target_link_libraries(nezumiiruka
PRIVATE
  core
  uicommon
  borealis
  imgui
  usbhsfs
)

set_source_files_properties(ShellAchievementsSwitch.cpp ShellCheatsSwitch.cpp ShellControlsSwitch.cpp
  ShellFilePickerSwitch.cpp ShellGameListSwitch.cpp ShellMemoryCardsSwitch.cpp ShellSettingsSwitch.cpp
  ShellSwitch.cpp
  ShellSystemSwitch.cpp
  ShellUpdaterSwitch.cpp
  PROPERTIES COMPILE_OPTIONS "${BOREALIS_EXCEPTIONS}")

if(ENABLE_VULKAN)
  # NXVK driver archives are self-contained and resolve backwards into Dolphin's own.
  target_link_libraries(nezumiiruka PRIVATE nxvk)
endif()

nx_generate_nacp(OUTPUT nezumiiruka.nacp
  NAME "Nezumiiruka"
  AUTHOR "Dolphin Emulator Project \& PalindromicBreadLoaf"
  VERSION "${NEZUMIIRUKA_VERSION}"
)

set(_sys_source "${CMAKE_SOURCE_DIR}/Data/Sys")
set(_sys_stamp "${CMAKE_CURRENT_BINARY_DIR}/romfs_sys.stamp")
file(GLOB_RECURSE _sys_files CONFIGURE_DEPENDS "${_sys_source}/*")
list(FILTER _sys_files EXCLUDE REGEX "^${_sys_source}/Themes/")
add_custom_command(
  OUTPUT "${_sys_stamp}"
  COMMAND "${CMAKE_COMMAND}" -DSRC=${_sys_source} -DDEST=${BOREALIS_ROMFS_DIR}/Sys
          -DSTAMP=${_sys_stamp} -P "${CMAKE_SOURCE_DIR}/CMake/StageSwitchSys.cmake"
  DEPENDS ${_sys_files} "${CMAKE_SOURCE_DIR}/CMake/StageSwitchSys.cmake"
  COMMENT "Staging Data/Sys into the romfs"
  VERBATIM)
add_custom_target(nezumiiruka_romfs_sys DEPENDS "${_sys_stamp}")

dkp_add_asset_target(nezumiiruka_romfs "${BOREALIS_ROMFS_DIR}")
add_dependencies(nezumiiruka_romfs borealis_romfs_files nezumiiruka_romfs_sys)
set_target_properties(nezumiiruka_romfs PROPERTIES
  DKP_ASSET_FILES "${BOREALIS_ROMFS_FILES};${_sys_stamp}")

# Defines the `nezumiiruka_nro` target that the Switch build is driven through.
nx_create_nro(nezumiiruka
  OUTPUT "${CMAKE_BINARY_DIR}/Binaries/nezumiiruka.nro"
  ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
  NACP "${CMAKE_CURRENT_BINARY_DIR}/nezumiiruka.nacp"
  ROMFS nezumiiruka_romfs
)
