#pragma once

// 1. Standard / System Headers first
#include <deque>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

// 2. External Library Headers
#include <shaders/shared/structs.h>
#include <vulkan/vulkan_core.h>

// 3. Project-wide utilities (Logger MUST be outside detail namespace)
#include <core/file_operations.hpp>
#include <core/logger.hpp>

// 4. The Slang Handshake (Keep the macro protection)
#pragma push_macro("None")
#pragma push_macro("Bool")
#undef None
#undef Bool
#include <slang-com-ptr.h>
#include <slang.h>
#pragma pop_macro("None")
#pragma pop_macro("Bool")
#include <shaders/shared/structs.h>
#include <vulkan/vulkan_core.h>

namespace detail
{

// A class responsible for compiling Slang source code.
class SlangCompiler
{
public:
  // Initializes the SlangCompiler.
  //
  // Set `enableGLSL` to `true` to enable the Slang compatibility module (which
  // is loaded when a Slang file includes a `#version` directive). If enabled,
  // you will also need to add `FILES ${Slang_GLSL_MODULE}` to your CMake
  // `copy_to_runtime_and_install` call.
  explicit SlangCompiler(bool enableGLSL = false);
  ~SlangCompiler() = default;

  void defaultTarget();   // Default target is SPIR-V
  void defaultOptions();  // Default options are EmitSpirvDirectly,
                          // VulkanUseEntryPointName

  void addOption(const slang::CompilerOptionEntry& option)
  {
    m_options.push_back(option);
  }
  void clearOptions()
  {
    m_options.clear();
  }
  std::vector<slang::CompilerOptionEntry>& options()
  {
    return m_options;
  }

  void addTarget(const slang::TargetDesc& target)
  {
    m_targets.push_back(target);
  }
  void clearTargets()
  {
    m_targets.clear();
  }
  std::vector<slang::TargetDesc>& targets()
  {
    return m_targets;
  }

  void addSearchPaths(const std::vector<std::filesystem::path>& searchPaths);
  void clearSearchPaths();
  // This is const because modifiying the search paths requires extra work.
  const std::vector<std::filesystem::path>& searchPaths() const
  {
    return m_searchPaths;
  }

  void addMacro(const slang::PreprocessorMacroDesc& macro)
  {
    m_macros.push_back(macro);
  }
  void clearMacros()
  {
    m_macros.clear();
  }
  std::vector<slang::PreprocessorMacroDesc>& macros()
  {
    return m_macros;
  }

  // Compile a file or source
  bool compileFile(const std::filesystem::path& filename);
  bool loadFromSourceString(const std::string& moduleName,
                            const std::string& slangSource);

  // Get result of the compilation
  const uint32_t* getSpirv() const;
  // Get the number of bytes in the compiled SPIR-V.
  size_t getSpirvSize() const;
  // Gets the linked Slang program; does not add a reference to it.
  // This is usually what you want for reflection.
  slang::IComponentType* getSlangProgram() const;
  // Gets the Slang module; does not add a reference to it. This is usually
  // useful for reflection if you need a list of functions.
  slang::IModule* getSlangModule() const;

  // Use for Dump or Aftermath
  void setCompileCallback(
      std::function<void(const std::filesystem::path& sourceFile,
                         const uint32_t* spirvCode, size_t spirvSize)>
          callback)
  {
    m_callback = callback;
  }

  // Get the last diagnostic message (error or warning).
  // Multiple diagnostics are each separated by a single newline.
  const std::string& getLastDiagnosticMessage() const
  {
    return m_lastDiagnosticMessage;
  }

private:
  void createSession();
  void logAndAppendDiagnostics(slang::IBlob* diagnostic);

  Slang::ComPtr<slang::IGlobalSession> m_globalSession;
  std::vector<slang::TargetDesc> m_targets;
  std::vector<slang::CompilerOptionEntry> m_options;
  std::vector<std::filesystem::path> m_searchPaths;
  std::vector<std::string> m_searchPathsUtf8;
  std::vector<const char*> m_searchPathsUtf8Pointers;
  Slang::ComPtr<slang::ISession> m_session;
  Slang::ComPtr<slang::IModule> m_module;
  Slang::ComPtr<slang::IComponentType> m_linkedProgram;
  Slang::ComPtr<ISlangBlob> m_spirv;
  std::vector<slang::PreprocessorMacroDesc> m_macros;

  std::function<void(const std::filesystem::path& sourceFile,
                     const uint32_t* spirvCode, size_t spirvSize)>
      m_callback;

  // Store the last diagnostic message
  std::string m_lastDiagnosticMessage;
};

}  // namespace detail

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
                                   const std::span<const uint32_t>& spirv = {},
                                   bool useCache = true);

  void clearCache()
  {
    m_binaryCacheMap.clear();
  }

  // Delete Copy/Move to enforce Singleton uniqueness
  SlangCompiler(const SlangCompiler&) = delete;
  SlangCompiler& operator=(const SlangCompiler&) = delete;
  SlangCompiler(SlangCompiler&&) = delete;
  SlangCompiler& operator=(SlangCompiler&&) = delete;

private:
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
  detail::SlangCompiler m_slangContext{};
  std::unordered_map<std::string, std::vector<uint32_t>> m_binaryCacheMap;
};
