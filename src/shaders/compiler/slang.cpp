#include "slang.hpp"

#include <nvslang/slang.hpp>
#include <nvutils/timers.hpp>

SlangShaderCompiler::SlangShaderCompiler(const std::vector<std::filesystem::path>& shader_dirs)
{
  m_shader_dirs = std::move(shader_dirs);
  // Setting up the Slang compiler for hot reload shader
  m_compiler.addSearchPaths(shader_dirs);
  m_compiler.defaultTarget();
  m_compiler.defaultOptions();
  m_compiler.addOption({slang::CompilerOptionName::DebugInformation,
                        {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_MAXIMAL}});
#if defined(AFTERMATH_AVAILABLE)
  // This aftermath callback is used to report the shader hash (Spirv) to the Aftermath library.
  m_compiler.setCompileCallback(
      [&](const std::filesystem::path& sourceFile, const uint32_t* spirvCode, size_t spirvSize)
      {
        std::span<const uint32_t> data(spirvCode, spirvSize / sizeof(uint32_t));
        AftermathCrashTracker::getInstance().addShaderBinary(data);
      });
#endif
}

// This function is used to compile the Slang shader, and when it fails, it will use the
// pre-compiled shaders
VkShaderModuleCreateInfo SlangShaderCompiler::compile(const std::filesystem::path& filename,
                                                      const std::span<const uint32_t>& spirv)
{
  SCOPED_TIMER(__FUNCTION__);

  // Use pre-compiled shaders by default
  VkShaderModuleCreateInfo shaderCode = getShaderModuleCreateInfo(spirv);

  // Try compiling the shader
  std::filesystem::path shaderSource = nvutils::findFile(filename, m_shader_dirs);
  if (m_compiler.compileFile(shaderSource))
  {
    // Using the Slang compiler to compile the shaders
    shaderCode.codeSize = m_compiler.getSpirvSize();
    shaderCode.pCode = m_compiler.getSpirv();
  }
  else
  {
    LOGE("Error compiling shaders: %s\n%s\n", shaderSource.string().c_str(),
         m_compiler.getLastDiagnosticMessage().c_str());
  }
  return shaderCode;
}
