# Copyright 2026 Dolphin Emulator Project
# Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
# SPDX-License-Identifier: GPL-2.0-or-later

add_library(videodeko3d
  Constants.h
  DKBoundingBox.cpp
  DKBoundingBox.h
  DKCommandBufferManager.cpp
  DKCommandBufferManager.h
  DKContext.cpp
  DKContext.h
  DKGfx.cpp
  DKGfx.h
  DKMain.cpp
  DKMemoryTracker.cpp
  DKMemoryTracker.h
  DKObjectCache.cpp
  DKObjectCache.h
  DKPerfQuery.cpp
  DKPerfQuery.h
  DKPipeline.cpp
  DKPipeline.h
  DKShader.cpp
  DKShader.h
  DKShaderCompiler.h
  DKStateTracker.cpp
  DKStateTracker.h
  DKStreamBuffer.cpp
  DKStreamBuffer.h
  DKSwapChain.cpp
  DKSwapChain.h
  DKTexture.cpp
  DKTexture.h
  DKVertexFormat.cpp
  DKVertexFormat.h
  DKVertexManager.cpp
  DKVertexManager.h
  VideoBackend.h
)

if(ENABLE_FRAME_GENERATION)
  target_sources(videodeko3d PRIVATE
    DKFrameGenerationShaders.cpp
    DKFrameGenerationShaders.h
    DKFrameGenerator.cpp
    DKFrameGenerator.h
  )
  target_link_libraries(videodeko3d PRIVATE lsfg)
endif()

target_link_libraries(videodeko3d
PUBLIC
  common
  videocommon

PRIVATE
  deko3d
)

if(NEZUMIIRUKA_OUNCE)
  target_sources(videodeko3d PRIVATE DKShaderCompilerSpirv.cpp)
  target_link_libraries(videodeko3d PRIVATE uam2rt)
else()
  target_sources(videodeko3d PRIVATE
    DKShaderCompiler.cpp
    UamBridge.h
    UamStderr.h
  )
  target_link_libraries(videodeko3d PRIVATE uam)
endif()
