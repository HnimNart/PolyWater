#include "slang.hpp"

#include <cstring>

#include "core/timers.hpp"

namespace
{

/**********************************************************/
void debugShaderMagic(const std::string& name,
                      const VkShaderModuleCreateInfo& info)
/**********************************************************/
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

/**********************************************************/
void SlangCompiler::init(const std::vector<std::filesystem::path>& shaderDirs)
/**********************************************************/
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
/**********************************************************/
VkShaderModuleCreateInfo
SlangCompiler::compile(const std::filesystem::path& filename,
                       const std::span<const uint32_t>& spirv, bool useCache)
/**********************************************************/
{

  SCOPED_TIMER(filename.string());
  std::string key = filename.string();

  // --- 1. Check Cache First ---
  auto it = m_binaryCacheMap.find(key);
  if (useCache && it != m_binaryCacheMap.end())
  {
    VkShaderModuleCreateInfo shaderCode{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderCode.codeSize = it->second.size() * sizeof(uint32_t);
    shaderCode.pCode = it->second.data();
    return shaderCode;
  }

  // --- 2. Find and Compile if not cached ---
  std::filesystem::path shaderSource = core::findFile(filename, m_shaderDirs);

  if (!shaderSource.empty() && m_slangContext.compileFile(shaderSource))
  {
    const uint32_t* rawData = m_slangContext.getSpirv();
    size_t dwordCount = m_slangContext.getSpirvSize() / sizeof(uint32_t);

    // Store in map
    std::vector<uint32_t>& cachedData = m_binaryCacheMap[key];
    cachedData.assign(rawData, rawData + dwordCount);

    VkShaderModuleCreateInfo shaderCode{
        VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    shaderCode.codeSize = cachedData.size() * sizeof(uint32_t);
    shaderCode.pCode = cachedData.data();
    return shaderCode;
  }

  // --- 3. Fallback logic ---
  if (!spirv.empty())
  {
    return getShaderModuleCreateInfo(spirv);
  }

  LOGE("Compilation failed for: %s", key.c_str());
  return {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
}

detail::SlangCompiler::SlangCompiler(bool enableGLSL)
{
  SlangGlobalSessionDesc desc{.enableGLSL = enableGLSL};
  slang::createGlobalSession(&desc, m_globalSession.writeRef());
}

void detail::SlangCompiler::defaultTarget()
{
  m_targets.push_back({
      .format = SLANG_SPIRV,
      .profile = m_globalSession->findProfile("spirv_1_6+vulkan_1_4"),
      .flags = SLANG_TARGET_FLAG_GENERATE_SPIRV_DIRECTLY,
      .forceGLSLScalarBufferLayout = true,
  });
}

void detail::SlangCompiler::defaultOptions()
{
  m_options.push_back({slang::CompilerOptionName::EmitSpirvDirectly,
                       {slang::CompilerOptionValueKind::Int, 1}});
  m_options.push_back({slang::CompilerOptionName::VulkanUseEntryPointName,
                       {slang::CompilerOptionValueKind::Int, 1}});
  // m_options.push_back({slang::CompilerOptionName::AllowGLSL,
  // {slang::CompilerOptionValueKind::Int, 1}});
}

void detail::SlangCompiler::addSearchPaths(
    const std::vector<std::filesystem::path>& searchPaths)
{
  for (auto& str : searchPaths)
  {
    m_searchPaths.push_back(str);  // For core::findFile()
    m_searchPathsUtf8.push_back(
        core::utf8FromPath(str));  // Need to keep the UTF-8 allocation alive
    // Slang expects const char* to UTF-8; see implementation of Slang's
    // FileStream::_init().
    m_searchPathsUtf8Pointers.push_back(m_searchPathsUtf8.back().c_str());
  }
}

void detail::SlangCompiler::clearSearchPaths()
{
  m_searchPaths.clear();
  m_searchPathsUtf8.clear();
  m_searchPathsUtf8Pointers.clear();
}

const uint32_t* detail::SlangCompiler::getSpirv() const
{
  if (!m_spirv)
  {
    return nullptr;
  }
  return reinterpret_cast<const uint32_t*>(m_spirv->getBufferPointer());
}

size_t detail::SlangCompiler::getSpirvSize() const
{
  if (!m_spirv)
  {
    return 0;
  }
  return m_spirv->getBufferSize();
}

slang::IComponentType* detail::SlangCompiler::getSlangProgram() const
{
  if (!m_linkedProgram)
  {
    return nullptr;
  }
  return m_linkedProgram.get();
}

slang::IModule* detail::SlangCompiler::getSlangModule() const
{
  if (!m_module)
  {
    return nullptr;
  }
  return m_module.get();
}

bool detail::SlangCompiler::compileFile(const std::filesystem::path& filename)
{
  const std::filesystem::path sourceFile =
      core::findFile(filename, m_searchPaths);
  if (sourceFile.empty())
  {
    m_lastDiagnosticMessage = "File not found: " + core::utf8FromPath(filename);
    LOGW("%s\n", m_lastDiagnosticMessage.c_str());
    return false;
  }
  bool success = loadFromSourceString(core::utf8FromPath(sourceFile.stem()),
                                      core::loadFile(sourceFile));
  if (success)
  {
    if (m_callback)
    {
      m_callback(sourceFile, getSpirv(), getSpirvSize());
    }
  }

  return success;
}

void detail::SlangCompiler::logAndAppendDiagnostics(slang::IBlob* diagnostics)
{
  if (diagnostics)
  {
    const char* message =
        reinterpret_cast<const char*>(diagnostics->getBufferPointer());
    // Since these are often multi-line, we want to print them with extra
    // spaces:
    LOGW("\n%s\n", message);
    // Append onto m_lastDiagnosticMessage, separated by a newline:
    if (m_lastDiagnosticMessage.empty())
    {
      m_lastDiagnosticMessage += '\n';
    }
    m_lastDiagnosticMessage += message;
  }
}

bool detail::SlangCompiler::loadFromSourceString(const std::string& moduleName,
                                                 const std::string& slangSource)
{
  createSession();

  // Clear any previous compilation
  m_spirv = nullptr;
  m_lastDiagnosticMessage.clear();

  Slang::ComPtr<slang::IBlob> diagnostics;
  // From source code to Slang module
  m_module = m_session->loadModuleFromSourceString(
      moduleName.c_str(), nullptr, slangSource.c_str(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if (!m_module)
  {
    return false;
  }

  // In order to get entrypoint shader reflection, it seems like one must go
  // through the additional step of listing every entry point in the composite
  // type. This matches the docs, but @nbickford wonders if there's a simpler
  // way.
  const SlangInt32 definedEntryPointCount =
      m_module->getDefinedEntryPointCount();
  std::vector<Slang::ComPtr<slang::IEntryPoint>> entryPoints(
      definedEntryPointCount);
  std::vector<slang::IComponentType*> components(1 + definedEntryPointCount);
  components[0] = m_module;
  for (SlangInt32 i = 0; i < definedEntryPointCount; i++)
  {
    m_module->getDefinedEntryPoint(i, entryPoints[i].writeRef());
    components[1 + i] = entryPoints[i];
  }

  Slang::ComPtr<slang::IComponentType> composedProgram;
  SlangResult result = m_session->createCompositeComponentType(
      components.data(), components.size(), composedProgram.writeRef(),
      diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if (SLANG_FAILED(result) || !composedProgram)
  {
    return false;
  }

  // From composite component type to linked program
  result =
      composedProgram->link(m_linkedProgram.writeRef(), diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if (SLANG_FAILED(result) || !m_linkedProgram)
  {
    return false;
  }

  // From linked program to SPIR-V
  result = m_linkedProgram->getTargetCode(0, m_spirv.writeRef(),
                                          diagnostics.writeRef());
  logAndAppendDiagnostics(diagnostics);
  if (SLANG_FAILED(result) || nullptr == m_spirv)
  {
    return false;
  }
  return true;
}

void detail::SlangCompiler::createSession()
{
  m_session = {};

  slang::SessionDesc desc{
      .targets = m_targets.data(),
      .targetCount = SlangInt(m_targets.size()),
      .searchPaths = m_searchPathsUtf8Pointers.data(),
      .searchPathCount = SlangInt(m_searchPathsUtf8Pointers.size()),
      .preprocessorMacros = m_macros.data(),
      .preprocessorMacroCount = SlangInt(m_macros.size()),
      .allowGLSLSyntax = true,
      .compilerOptionEntries = m_options.data(),
      .compilerOptionEntryCount = uint32_t(m_options.size()),
  };
  m_globalSession->createSession(desc, m_session.writeRef());
}
