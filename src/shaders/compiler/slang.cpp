#include "slang.hpp"

#include <nvslang/slang.hpp>

#include "common/timers.hpp"

void SlangCompiler::init(const std::vector<std::filesystem::path>& shaderDirs)
{
  m_shaderDirs = shaderDirs;
  // Setting up the Slang compiler for hot reload shader
  m_slangContext.addSearchPaths(shaderDirs);
  m_slangContext.defaultTarget();
  m_slangContext.defaultOptions();
  m_slangContext.addOption({slang::CompilerOptionName::DebugInformation,
                            {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_MAXIMAL}});
#if defined(AFTERMATH_AVAILABLE)
  // This aftermath callback is used to report the shader hash (Spirv) to the Aftermath library.
  m_slangContext.setCompileCallback(
      [&](const std::filesystem::path& sourceFile, const uint32_t* spirvCode, size_t spirvSize)
      {
        std::span<const uint32_t> data(spirvCode, spirvSize / sizeof(uint32_t));
        AftermathCrashTracker::getInstance().addShaderBinary(data);
      });
#endif
}

// This function is used to compile the Slang shader, and when it fails, it will use the
// pre-compiled shaders
VkShaderModuleCreateInfo SlangCompiler::compile(const std::filesystem::path& filename,
                                                const std::span<const uint32_t>& spirv)
{
  common::ScopedTimer(filename.string());

  // Use pre-compiled shaders by default
  VkShaderModuleCreateInfo shaderCode = getShaderModuleCreateInfo(spirv);

  // Try compiling the shader
  std::filesystem::path shaderSource = nvutils::findFile(filename, m_shaderDirs);
  if (m_slangContext.compileFile(shaderSource))
  {
    // Using the Slang compiler to compile the shaders
    shaderCode.codeSize = m_slangContext.getSpirvSize();
    shaderCode.pCode = m_slangContext.getSpirv();
  }
  else
  {
    LOGE("Error compiling shaders: %s\n%s\n", shaderSource.string().c_str(),
         m_slangContext.getLastDiagnosticMessage().c_str());
  }
  return shaderCode;
}
