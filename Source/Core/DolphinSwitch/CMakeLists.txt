add_executable(porpoise
  ControllerProfilesSwitch.cpp
  ControllerProfilesSwitch.h
  CrashHandlerSwitch.cpp
  HorizonCompatSwitch.cpp
  HorizonExceptionEntry.h
  HorizonExceptionEntry.S
  HostSwitch.cpp
  MainSwitch.cpp
  PerformanceOverlaySwitch.cpp
  PlatformSwitch.cpp
  SettingsSwitch.cpp
  SettingsSwitch.h
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
  ShellSettingsSwitch.cpp
  ShellSettingsSwitch.h
  ShellSwitch.cpp
  ShellSwitch.h
  ShellSystemSwitch.cpp
  ShellSystemSwitch.h
)

if(USE_RETRO_ACHIEVEMENTS)
  target_sources(porpoise PRIVATE
    ShellAchievementsSwitch.cpp
    ShellAchievementsSwitch.h
  )
endif()

target_link_libraries(porpoise
PRIVATE
  core
  uicommon
  borealis
)

set_source_files_properties(ShellAchievementsSwitch.cpp ShellControlsSwitch.cpp
  ShellFilePickerSwitch.cpp ShellGameListSwitch.cpp ShellSettingsSwitch.cpp ShellSwitch.cpp
  ShellSystemSwitch.cpp
  PROPERTIES COMPILE_OPTIONS "${BOREALIS_EXCEPTIONS}")

if(ENABLE_VULKAN)
  # NXVK driver archives are self-contained and resolve backwards into Dolphin's own.
  target_link_libraries(porpoise PRIVATE nxvk)
endif()

nx_generate_nacp(OUTPUT porpoise.nacp
  NAME "Porpoise"
  AUTHOR "Dolphin Emulator Project \& PalindromicBreadLoaf"
  VERSION "${PORPOISE_VERSION}"
)

dkp_add_asset_target(porpoise_romfs "${BOREALIS_ROMFS_DIR}")
add_dependencies(porpoise_romfs borealis_romfs_files)
set_target_properties(porpoise_romfs PROPERTIES DKP_ASSET_FILES "${BOREALIS_ROMFS_FILES}")

# Defines the `porpoise_nro` target that the Switch build is driven through.
nx_create_nro(porpoise
  OUTPUT "${CMAKE_BINARY_DIR}/Binaries/porpoise.nro"
  ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
  NACP "${CMAKE_CURRENT_BINARY_DIR}/porpoise.nacp"
  ROMFS porpoise_romfs
)
