#pragma once

#include <cstdint>
#include <map>
#include <string>

#include "gltf/io_gltf.h"

struct MaterialEntry
{
  MaterialType type;
  std::string
      closestHitSymbol;   // The function name in Slang (e.g., "rchitGlass")
  uint32_t sbtIndex = 0;  // Computed during pipeline build
};

class MaterialManager
{
public:
  MaterialManager() = default;
  void registerMaterial(MaterialType type, const std::string& shaderSymbol)
  {
    auto it = m_registry.find(type);
    assert(it == m_registry.end() &&
           "MaterialType already registered in the registry!");
    m_registry[type] = {type, shaderSymbol};
  }

  // Get the index to put into the VkAccelerationStructureInstanceKHR
  uint32_t getSbtOffset(MaterialType type) const
  {
    return m_registry.at(type).sbtIndex;
  }

  auto& getRegistry() { return m_registry; }
  const auto& getRegistry() const { return m_registry; }

private:
  std::map<MaterialType, MaterialEntry> m_registry;
};
