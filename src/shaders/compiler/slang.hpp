#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan_core.h>

#include <filesystem>
#include <nvslang/slang.hpp>

class SlangShaderCompiler
{
public:
  SlangShaderCompiler() = default;
  SlangShaderCompiler(const std::vector<std::filesystem::path>& shader_dirs);
  VkShaderModuleCreateInfo compile(const std::filesystem::path& filename,
                                   const std::span<const uint32_t>& spirv);

private:
  inline VkShaderModuleCreateInfo getShaderModuleCreateInfo(const std::span<const uint32_t>& spirv)
  {
    return VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size_bytes(),
        .pCode = spirv.data(),
    };
  }
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
