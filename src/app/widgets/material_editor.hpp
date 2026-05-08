#pragma once

#include "scene/scene_resources.hpp"

namespace app
{

class MaterialEditor
{
public:
  bool render(scene::SceneResourcesManager& resources);

private:
  char m_search[128] = {};
};

}  // namespace app
