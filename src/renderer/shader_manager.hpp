#pragma once

#include <cstdint>
#include <map>
#include <span>
#include <string>

#include "shaders/shared/structs.h"

struct MaterialEntry
{
  MaterialType type;
  std::string prettyName;  // "diffuse"
  std::string filename;    // e.g., "diffuse.slang"
  std::string entryPoint;  // e.g., "rchit_diffuse"
  uint32_t sbtIndex = 0;
  std::span<const uint32_t> spirv;
};

struct RaygenEntry
{
  std::string prettyName;  // "diffuse"
  std::string filename;
  std::string entryPoint;
  std::span<const uint32_t> spirv;
};

struct MissEntry
{
  std::string prettyName;  // "diffuse"
  std::string filename;
  std::string entryPoint;
  std::span<const uint32_t> spirv;
};

class ShaderManager
{
public:
  ShaderManager();

  /**
   * @brief Registers a material and its associated shader metadata.
   * @param baseName The base name of the shader (e.g., "glass" becomes
   * "glass.slang")
   */
  void registerMaterial(MaterialType type, const std::string& baseName,
                        std::span<const uint32_t> spirv = {});

  uint32_t getSbtOffset(MaterialType type) const;

  // Accessors
  std::map<MaterialType, MaterialEntry>& getRegistry();
  const std::map<MaterialType, MaterialEntry>& getRegistry() const;

  const RaygenEntry& getRaygen() const;
  const MissEntry& getMiss() const;
  const MissEntry& getShadowMiss() const;

private:
  std::map<MaterialType, MaterialEntry> m_registry;
  RaygenEntry m_raygen;
  MissEntry m_miss;
  MissEntry m_shadowMiss;
};
