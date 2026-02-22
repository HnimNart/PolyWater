#pragma once

#include <glm/glm.hpp>

#include <glm/ext/quaternion_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "property_editor.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"

namespace app {

inline bool renderEditor(SceneResourcesManager &resources,
                         IRenderer *renderer) {
  namespace PE = app::PropertyEditor;
  bool hasChanged = false;
  if (PE::begin("RenderModeTable")) {
    RenderMode m_renderMode = renderer->getRenderMode();
    const char *preview = renderModeToString(m_renderMode);
    if (PE::entry("Mode", [&]() {
          bool changed = false;
          if (ImGui::BeginCombo("##mode", preview)) {
            for (int n = 0; n < static_cast<int>(RenderMode::COUNT); n++) {
              auto mode = static_cast<RenderMode>(n);
              if (ImGui::Selectable(renderModeToString(mode),
                                    m_renderMode == mode)) {
                m_renderMode = mode;
                renderer->setRenderMode(m_renderMode);
                resources.setDirty(true);
                changed = true;
              }
            }
            ImGui::EndCombo();
          }
          return changed;
        })) {
      hasChanged = true;
    }

    if (m_renderMode == RenderMode::RAYTRACE) {
      shaderio::RenderParams &params = renderer->renderParams();
      hasChanged |= PE::DragInt("Samples", &params.nSamples, 1.0F, 0, 1024);
      hasChanged |=
          PE::DragInt("Max Bounces", &params.maxBounces, 1.0F, 0, 1024);
      if (PE::Button("Reset Accumulation", ImVec2(-1.0f, 0.0f))) {
        hasChanged = true;
      }
    } else {
      shaderio::RasterParams &params = renderer->rasterParams();
      if (PE::Checkbox("Wireframe Mode", (bool *)&params.wireframe)) {
        hasChanged = true;
      }
      if (params.wireframe) {
        if (PE::SliderFloat("Line Width", &params.wireframeLineWidth, 0.1f,
                            10.0f)) {
          hasChanged = true;
        }
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                           "Note: Wide lines require hardware support.");
      }
    }

    PE::end();
  }
  return hasChanged;
}

} // namespace app
