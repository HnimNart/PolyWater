#pragma once

#include <glm/ext/quaternion_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "property_editor.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"

namespace app
{

/**********************************************************/
inline bool renderEditor(SceneResourcesManager& resources, IRenderer* renderer)
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  bool hasChanged = false;

  if (PE::begin("RenderModeTable"))
  {
    // Fetch current mode and available modes
    std::string currentMode = renderer->getCurrentMode();
    const std::vector<std::string> availableModes =
        renderer->getAvaliableModes();

    if (PE::entry("Mode",
                  [&]()
                  {
                    bool changed = false;
                    // Use the current mode string as the preview
                    if (ImGui::BeginCombo("##mode", currentMode.c_str()))
                    {
                      for (const std::string& mode : availableModes)
                      {
                        bool isSelected = (currentMode == mode);

                        if (ImGui::Selectable(mode.c_str(), isSelected))
                        {
                          currentMode = mode;
                          renderer->setRenderMode(currentMode);
                          resources.setDirty(true);
                          changed = true;
                        }

                        // Set the initial focus when opening the combo
                        if (isSelected)
                        {
                          ImGui::SetItemDefaultFocus();
                        }
                      }
                      ImGui::EndCombo();
                    }
                    return changed;
                  }))
    {
      hasChanged = true;
    }

    // Update conditional checks to use the string literals from your
    // PipelineManager
    if (currentMode == "Raytrace")
    {
      shaderio::RenderParams& params = renderer->renderParams();
      hasChanged |= PE::DragInt("Samples", &params.nSamples, 1.0F, 0, 1024);
      hasChanged |=
          PE::DragInt("Max Bounces", &params.maxBounces, 1.0F, 0, 1024);
      bool denoiseEnabled = (params.denoise > 0);
      if (PE::Checkbox("Denoise", &denoiseEnabled))
      {
        params.denoise = denoiseEnabled ? 1 : 0;
      }

      // --- Conditionally show Denoiser Settings ---
      if (0 && denoiseEnabled)
      {
        // Assuming your PE wrapper supports SliderFloat. If not, use DragFloat.
        hasChanged |=
            PE::SliderFloat("Blur Radius", &params.denoiseRadius, 1.0f, 10.0f);
        hasChanged |= PE::SliderFloat("Spatial Sigma",
                                      &params.denoiseSpatialSigma, 0.1f, 10.0f);
        hasChanged |= PE::SliderFloat(
            "Luminance Sigma", &params.denoiseLuminanceSigma, 0.01f, 2.0f);
      }

      if (PE::Button("Reset Accumulation", ImVec2(-1.0f, 0.0f)))
      {
        hasChanged = true;
      }
    }
    else
    {
      // Handles both "Raster" and "Meshlet" modes (or any other raster-based
      // graph)
      shaderio::RasterParams& params = renderer->rasterParams();
      if (PE::Checkbox("Wireframe Mode", (bool*) &params.wireframe))
      {
        hasChanged = true;
      }
      if (params.wireframe)
      {
        if (PE::SliderFloat("Line Width", &params.wireframeLineWidth, 0.1f,
                            10.0f))
        {
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

}  // namespace app
