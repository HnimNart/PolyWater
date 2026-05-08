#pragma once

#include "backend/interfaces/rhi_definitions.hpp"
#include "renderer/interfaces/renderer_interface.hpp"
#include "scene/scene_resources.hpp"

namespace app
{

class RenderEditor
{
public:
  // Renders the render-mode and output-selection UI.
  // Returns true if any change requires the renderer to reset/redraw.
  bool render(scene::SceneResourcesManager& resources, IRenderer* renderer);

  // Returns the currently selected render output for viewport display.
  RenderOutput currentOutput() const { return m_currentOutput; }

private:
  RenderOutput m_currentOutput = RenderOutput::ToneMapped;
};

}  // namespace app
