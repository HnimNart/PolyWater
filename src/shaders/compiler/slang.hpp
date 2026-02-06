#pragma once

#include <shaders/shaderio.h>
#include <vulkan/vulkan_core.h>

#include <deque>
#include <filesystem>
#include <span>
#include <vector>

#include <nvslang/slang.hpp>

class SlangCompiler
{
public:
  // 1. Singleton Accessor
  static SlangCompiler& instance()
  {
    static SlangCompiler s_instance;
    return s_instance;
  }

  void init(const std::vector<std::filesystem::path>& shaderDirs);
  VkShaderModuleCreateInfo compile(const std::filesystem::path& filename,
                                   const std::span<const uint32_t>& spirv = {});

  // Delete Copy/Move to enforce Singleton uniqueness
  SlangCompiler(const SlangCompiler&) = delete;
  SlangCompiler& operator=(const SlangCompiler&) = delete;
  SlangCompiler(SlangCompiler&&) = delete;
  SlangCompiler& operator=(SlangCompiler&&) = delete;

private:
  // Private Constructor
  SlangCompiler() = default;

  // Internal Helper
  inline VkShaderModuleCreateInfo
  getShaderModuleCreateInfo(const std::span<const uint32_t>& spirv) const
  {
    return VkShaderModuleCreateInfo{
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = spirv.size_bytes(),
        .pCode = spirv.data(),
    };
  }

  // Member Variables
  bool m_initialized = false;
  std::vector<std::filesystem::path> m_shaderDirs;
  nvslang::SlangCompiler m_slangContext{};
  std::deque<std::vector<uint32_t>> m_binaryCache;
};
