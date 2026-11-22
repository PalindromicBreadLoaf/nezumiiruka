// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include "VideoBackends/Deko3D/DKShaderCompiler.h"

#include <cstdlib>
#include <string>
#include <string_view>

#include <uam2rt.h>

#include "Common/Logging/Log.h"
#include "Common/Timer.h"

#include "VideoBackends/Deko3D/Constants.h"

#include "VideoCommon/ShaderCompileUtils.h"
#include "VideoCommon/Spirv.h"

namespace Deko3D::ShaderCompiler
{
namespace
{
static_assert(TEXEL_BUFFER_BINDING_BASE == 16, "TEXEL_BUFFER_BINDING below is out of date");
constexpr char SHADER_HEADER[] = R"(
  #version 450 core

  #extension GL_ARB_shading_language_include : enable

  #define ATTRIBUTE_LOCATION(x) layout(location = x)
  #define FRAGMENT_OUTPUT_LOCATION(x) layout(location = x)
  #define FRAGMENT_OUTPUT_LOCATION_INDEXED(x, y) layout(location = x, index = y)
  #define UBO_BINDING(packing, x) layout(packing, binding = (x - 1))
  #define SAMPLER_BINDING(x) layout(binding = x)
  #define TEXEL_BUFFER_BINDING(x) layout(binding = (x + 16))
  #define SSBO_BINDING(x) layout(std430, binding = x)
  #define VARYING_LOCATION(x) layout(location = x)
  #define FORCE_EARLY_Z layout(early_fragment_tests) in

  // hlsl to glsl function translation
  #define API_VULKAN 1
  #define float2 vec2
  #define float3 vec3
  #define float4 vec4
  #define uint2 uvec2
  #define uint3 uvec3
  #define uint4 uvec4
  #define int2 ivec2
  #define int3 ivec3
  #define int4 ivec4
  #define frac fract
  #define lerp mix

  #define gl_VertexID gl_VertexIndex
  #define gl_InstanceID gl_InstanceIndex
)";

constexpr char COMPUTE_SHADER_HEADER[] = R"(
  #version 450 core

  #extension GL_ARB_shading_language_include : enable

  #define UBO_BINDING(packing, x) layout(packing, binding = (x - 1))
  #define SAMPLER_BINDING(x) layout(binding = x)
  #define TEXEL_BUFFER_BINDING(x) layout(binding = (x + 16))
  #define IMAGE_BINDING(format, x) layout(format, binding = x)

  #define API_VULKAN 1
  #define float2 vec2
  #define float3 vec3
  #define float4 vec4
  #define uint2 uvec2
  #define uint3 uvec3
  #define uint4 uvec4
  #define int2 ivec2
  #define int3 ivec3
  #define int4 ivec4
  #define frac fract
  #define lerp mix
)";

constexpr glslang::EShTargetLanguageVersion SPIRV_VERSION = glslang::EShTargetSpv_1_0;

Uam2rtStage StageToUam2rt(ShaderStage stage)
{
  switch (stage)
  {
  case ShaderStage::Vertex:
    return Uam2rtStage_Vertex;
  case ShaderStage::Geometry:
    return Uam2rtStage_Geometry;
  case ShaderStage::Pixel:
    return Uam2rtStage_Fragment;
  case ShaderStage::Compute:
    return Uam2rtStage_Compute;
  default:
    return Uam2rtStage_Vertex;
  }
}

std::optional<SPIRV::CodeVector> CompileToSpirv(ShaderStage stage, const std::string& source,
                                                VideoCommon::ShaderIncluder* shader_includer)
{
  switch (stage)
  {
  case ShaderStage::Vertex:
    return SPIRV::CompileVertexShader(source, APIType::Vulkan, SPIRV_VERSION, shader_includer);
  case ShaderStage::Geometry:
    return SPIRV::CompileGeometryShader(source, APIType::Vulkan, SPIRV_VERSION, shader_includer);
  case ShaderStage::Pixel:
    return SPIRV::CompileFragmentShader(source, APIType::Vulkan, SPIRV_VERSION, shader_includer);
  case ShaderStage::Compute:
    return SPIRV::CompileComputeShader(source, APIType::Vulkan, SPIRV_VERSION, shader_includer);
  default:
    return std::nullopt;
  }
}

bool InitializeUam2rt()
{
  static const bool initialized = uam2rt_init();
  return initialized;
}
}  // namespace

std::optional<std::vector<u8>> CompileShader(ShaderStage stage, std::string_view source,
                                             VideoCommon::ShaderIncluder* shader_includer,
                                             std::string_view name)
{
  if (!InitializeUam2rt())
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: uam2rt failed to initialise");
    return std::nullopt;
  }

  const std::string_view header =
      stage == ShaderStage::Compute ? COMPUTE_SHADER_HEADER : SHADER_HEADER;

  std::string full_source;
  full_source.reserve(header.size() + source.size());
  full_source.append(header);
  full_source.append(source);

  Common::Timer timer;
  timer.Start();

  const auto spirv = CompileToSpirv(stage, full_source, shader_includer);
  if (!spirv)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: glslang rejected '{}'", name);
    return std::nullopt;
  }

  const u64 glslang_ms = timer.ElapsedMs();

  size_t dksh_size = 0;
  char* log = nullptr;
  void* const dksh = uam2rt_compile_spirv(spirv->data(), spirv->size(), StageToUam2rt(stage),
                                          "main", nullptr, 0, &dksh_size, &log);

  const bool have_log = log != nullptr && log[0] != '\0';
  if (!dksh)
  {
    ERROR_LOG_FMT(VIDEO, "deko3d: uam2rt rejected '{}':\n{}", name,
                  have_log ? log : "(no diagnostics)");
    std::free(log);
    return std::nullopt;
  }

  if (have_log)
    WARN_LOG_FMT(VIDEO, "deko3d: uam2rt warnings for '{}':\n{}", name, log);
  std::free(log);

  DEBUG_LOG_FMT(VIDEO, "deko3d: compiled '{}' to {} bytes of DKSH in {} ms ({} ms spent in glslang)",
                name, dksh_size, timer.ElapsedMs(), glslang_ms);

  const auto* bytes = static_cast<const u8*>(dksh);
  std::vector<u8> blob(bytes, bytes + dksh_size);
  std::free(dksh);
  return blob;
}
}  // namespace Deko3D::ShaderCompiler
