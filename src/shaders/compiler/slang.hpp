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
  SlangShaderCompiler() = default;
  SlangShaderCompiler(const std::vector<std::filesystem::path>& shader_dirs);
  VkShaderModuleCreateInfo compile(const std::filesystem::path& filename,
                                   const std::span<const uint32_t>& spirv);

private:
  std::vector<std::filesystem::path> m_shader_dirs;
  nvslang::SlangCompiler m_compiler{};
};

// class SlangCompiler
// {
// public:
//   static SlangCompiler& instance()
//   {
//     static SlangCompiler instance;
//     return instance;
//   }

//   /// Must be called exactly once before use
//   void initialize(std::vector<std::filesystem::path> shaderDirs)
//   {
//     if (m_initialized)
//       throw std::logic_error("SlangCompiler already initialized");

//     m_compiler = SlangShaderCompiler(std::move(shaderDirs));
//     m_initialized = true;
//   }

//   SlangShaderCompiler& compiler()
//   {
//     if (!m_initialized)
//       throw std::logic_error("SlangCompiler not initialized");
//     return m_compiler;
//   }

//   const SlangShaderCompiler& compiler() const
//   {
//     if (!m_initialized)
//       throw std::logic_error("SlangCompiler not initialized");
//     return m_compiler;
//   }

//   // Non-copyable / non-movable
//   SlangCompiler(const SlangCompiler&) = delete;
//   SlangCompiler& operator=(const SlangCompiler&) = delete;
//   SlangCompiler(SlangCompiler&&) = delete;
//   SlangCompiler& operator=(SlangCompiler&&) = delete;

// private:
//   SlangCompiler() = default;

//   bool m_initialized = false;
//   SlangShaderCompiler m_compiler;
// };
