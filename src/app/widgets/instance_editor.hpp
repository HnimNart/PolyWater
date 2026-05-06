#pragma once

#include <map>

#include "renderer/shader_manager.hpp"
#include "scene/scene_resources.hpp"

namespace app
{

class InstanceEditor
{
public:
  bool render(SceneResourcesManager&                            resources,
              const std::map<MaterialType, MaterialEntry>& shaderRegistry);

private:
  char m_search[128] = {};
};

}  // namespace app
