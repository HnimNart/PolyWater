#include "slang.hpp"

#include <cstring>

#include <nvslang/slang.hpp>

#include "common/timers.hpp"

namespace
{
void debugShaderMagic(const std::string& name,
                      const VkShaderModuleCreateInfo& info)
{
  if (info.pCode == nullptr || info.codeSize == 0)
  {
    LOGE("Shader %s: pCode is NULL or size is 0!", name.c_str());
    return;
  }

  // SPIR-V Magic Number is always 0x07230203
  const uint32_t SPIRV_MAGIC = 0x07230203;
  uint32_t firstWord = info.pCode[0];

  if (firstWord == SPIRV_MAGIC)
  {
    LOGI("Shader %s: Valid SPIR-V Magic Number found.", name.c_str());
  }
  else
  {
    // Interpret the first 4 bytes as ASCII characters
    char chars[5];
    memcpy(chars, &firstWord, 4);
    chars[4] = '\0';

    LOGE("Shader %s: INVALID MAGIC NUMBER!", name.c_str());
    LOGE("  Expected: 0x%08x", SPIRV_MAGIC);
    LOGE("  Found:    0x%08x (ASCII interpretation: '%s')", firstWord, chars);

    // Print the next few words just in case
    if (info.codeSize >= 12)
    {
      LOGE("  Next words: 0x%08x, 0x%08x", info.pCode[1], info.pCode[2]);
    }
  }
}

}  // namespace

void SlangCompiler::init(const std::vector<std::filesystem::path>& shaderDirs)
{
  m_shaderDirs = shaderDirs;
  // Setting up the Slang compiler for hot reload shader
  m_slangContext.addSearchPaths(shaderDirs);
  m_slangContext.defaultTarget();
  m_slangContext.defaultOptions();
  m_slangContext.addOption(
      {slang::CompilerOptionName::DebugInformation,
       {slang::CompilerOptionValueKind::Int, SLANG_DEBUG_INFO_LEVEL_MAXIMAL}});
#if defined(AFTERMATH_AVAILABLE)
  // This aftermath callback is used to report the shader hash (Spirv) to the
  // Aftermath library.
  m_slangContext.setCompileCallback(
      [&](const std::filesystem::path& sourceFile, const uint32_t* spirvCode,
          size_t spirvSize)
      {
        std::span<const uint32_t> data(spirvCode, spirvSize / sizeof(uint32_t));
        AftermathCrashTracker::getInstance().addShaderBinary(data);
      });
#endif
}

// This function is used to compile the Slang shader, and when it fails, it will
// use the pre-compiled shaders
VkShaderModuleCreateInfo
SlangCompiler::compile(const std::filesystem::path& filename,
                       const std::span<const uint32_t>& spirv)
{
  SCOPED_TIMER(filename.string());

  // 1. Compile the file using your existing context
  std::filesystem::path shaderSource =
      nvutils::findFile(filename, m_shaderDirs);

  if (!shaderSource.empty() && m_slangContext.compileFile(shaderSource))
  {
    // 2. Deep copy the SPIR-V into our persistent cache
    const uint32_t* rawData = m_slangContext.getSpirv();
    size_t dwordCount = m_slangContext.getSpirvSize() / sizeof(uint32_t);

    // Add a new vector to the deque and copy data into it
    m_binaryCache.emplace_back(rawData, rawData + dwordCount);

    // 3. Point the VkShaderModuleCreateInfo to our cache
    VkShaderModuleCreateInfo shaderCode{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderCode.codeSize = m_binaryCache.back().size() * sizeof(uint32_t);
    shaderCode.pCode = m_binaryCache.back().data();
    return shaderCode;
  }

  // Fallback logic
  if (!spirv.empty())
  {
    return getShaderModuleCreateInfo(spirv);
  }

  LOGE("Compilation failed for: %s", filename.string().c_str());
  return {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
}
