add_executable(nezumiruka
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

target_compile_definitions(nezumiruka PRIVATE NEZUMIRUKA_VERSION="${NEZUMIRUKA_VERSION}")

if(USE_RETRO_ACHIEVEMENTS)
  target_sources(nezumiruka PRIVATE
    ShellAchievementsSwitch.cpp
    ShellAchievementsSwitch.h
  )
endif()

target_link_libraries(nezumiruka
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
  target_link_libraries(nezumiruka PRIVATE nxvk)
endif()

nx_generate_nacp(OUTPUT nezumiruka.nacp
  NAME "Nezumiruka"
  AUTHOR "Dolphin Emulator Project \& PalindromicBreadLoaf"
  VERSION "${NEZUMIRUKA_VERSION}"
)

dkp_add_asset_target(nezumiruka_romfs "${BOREALIS_ROMFS_DIR}")
add_dependencies(nezumiruka_romfs borealis_romfs_files)
set_target_properties(nezumiruka_romfs PROPERTIES DKP_ASSET_FILES "${BOREALIS_ROMFS_FILES}")

# Defines the `nezumiruka_nro` target that the Switch build is driven through.
nx_create_nro(nezumiruka
  OUTPUT "${CMAKE_BINARY_DIR}/Binaries/nezumiruka.nro"
  ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg"
  NACP "${CMAKE_CURRENT_BINARY_DIR}/nezumiruka.nacp"
  ROMFS nezumiruka_romfs
)
