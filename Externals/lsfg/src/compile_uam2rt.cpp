// Copyright 2026 Dolphin Emulator Project
// Copyright 2026 PalindromicBreadLoaf (palindromicbreadloaf@tuta.com)
// SPDX-License-Identifier: GPL-2.0-or-later

#include <lsfg/common/dksh.hpp>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

#include <uam2rt.h>

#include <cstdlib>
#include <memory>
#include <vector>

namespace lsfg::dksh {
namespace {

bool compile_to_spirv(const std::string_view glsl, std::vector<std::uint32_t>& out, std::string& log) {
    static const bool initialised = glslang::InitializeProcess();
    if (!initialised) {
        return false;
    }

    const auto messages = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    const char* const source = glsl.data();
    const int length = static_cast<int>(glsl.size());

    glslang::TShader shader(EShLangCompute);
    shader.setStringsWithLengths(&source, &length, 1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    glslang::TShader::ForbidIncluder includer;
    if (!shader.parse(GetDefaultResources(), 450, ECoreProfile, false, true, messages, includer)) {
        log.assign(shader.getInfoLog());
        return false;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(messages)) {
        log.assign(program.getInfoLog());
        return false;
    }

    glslang::SpvOptions options;
    options.disableOptimizer = false;
    options.stripDebugInfo = true;
    spv::SpvBuildLogger logger;
    glslang::GlslangToSpv(*program.getIntermediate(EShLangCompute), out, &logger, &options);
    return !out.empty();
}

} // namespace

bool compiler_available() noexcept {
    return true;
}

ErrorCode compile(const std::string_view glsl, Blob& out) {
    out = Blob{};

    if (glsl.empty()) {
        return ErrorCode::invalid_argument;
    }

    static const bool initialised = uam2rt_init();
    if (!initialised) {
        return ErrorCode::shader_compile_failed;
    }

    std::vector<std::uint32_t> spirv;
    if (!compile_to_spirv(glsl, spirv, out.log)) {
        return ErrorCode::shader_compile_failed;
    }

    std::size_t size = 0;
    char* log = nullptr;
    void* const dksh = uam2rt_compile_spirv(
        spirv.data(), spirv.size(), Uam2rtStage_Compute, "main", nullptr, 0, &size, &log);

    if (log != nullptr) {
        out.log.assign(log);
        std::free(log);
    }

    if (dksh == nullptr || size == 0) {
        std::free(dksh);
        return ErrorCode::shader_compile_failed;
    }

    const auto* const bytes = static_cast<const std::uint8_t*>(dksh);
    out.bytes.assign(bytes, bytes + size);
    std::free(dksh);

    return validate(out.bytes, out.program);
}

} // namespace lsfg::dksh
