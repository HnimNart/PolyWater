#pragma once

#include "scene/scene_resources.hpp"

namespace app
{

class MaterialEditor
{
public:
  bool render(SceneResourcesManager& resources);

private:
  char m_search[128] = {};
};

}  // namespace app
