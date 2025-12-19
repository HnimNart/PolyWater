#pragma once

#include <shaders/shaderio.h>

#include <filesystem>
#include <nvshaders_host/sky.hpp>
#include <nvshaders_host/tonemapper.hpp>
#include <nvslang/slang.hpp>
#include <nvutils/camera_manipulator.hpp>
#include <nvvk/acceleration_structures.hpp>  // Acceleration structure management
#include <nvvk/descriptors.hpp>
#include <nvvk/gbuffers.hpp>  // GBuffer management
#include <nvvk/graphics_pipeline.hpp>
#include <nvvk/sampler_pool.hpp>
#include <nvvk/sbt_generator.hpp>

class SlangShaderCompiler
{
public:
  SlangShaderCompiler(const std::vector<std::filesystem::path>& shader_dirs);
  VkShaderModuleCreateInfo compile(const std::filesystem::path& filename,
                                   const std::span<const uint32_t>& spirv);

private:
  std::vector<std::filesystem::path> m_shader_dirs;
  nvslang::SlangCompiler m_compiler{};
};
