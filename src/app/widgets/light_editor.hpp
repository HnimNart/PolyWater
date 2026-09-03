#pragma once

#include <memory>

#include "scene/scene_resources.hpp"
#include "sky.hpp"

class IDeviceAssets;

namespace app
{

class LightEditor
{
public:
  LightChangedBitMask render(scene::SceneResourcesManager&             resources,
                             const std::shared_ptr<IDeviceAssets>& deviceResources);

private:
  SkyEditor m_skyEditor;
};

}  // namespace app
