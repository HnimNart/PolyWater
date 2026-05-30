#include "render_editor.hpp"

#include <imgui.h>

#include "property_editor.hpp"
#include "shaders/shared/structs.h"

namespace app
{

// Human-readable labels for each RenderOutput value (order must match the enum)
static constexpr const char* kOutputNames[RenderOutput::Count] = {
    "Linear (HDR raw)",      // Linear = 0
    "ToneMapped (SDR)",      // ToneMapped = 1
    "Accum Linear (HDR)",    // AccumLinear = 2
    "Denoised (HDR)",        // Denoised = 3
    "Albedo",                // Albedo = 4
    "Normal (World-space)",  // Normal = 5
    "Depth Buffer",          // DepthBuffer = 6
    "Swapchain",             // Swapchain = 7
};

/**********************************************************/
bool RenderEditor::render(scene::SceneResourcesManager& resources,
                          IRenderer* renderer)
/**********************************************************/
{
  namespace PE = app;
  bool hasChanged = false;

  if (PE::begin("RenderModeTable"))
  {
    // ---- Render mode selection ----
    std::string currentMode = renderer->getCurrentMode();
    const std::vector<std::string> availableModes =
        renderer->getAvaliableModes();

    if (PE::entry("Mode",
                  [&]()
                  {
                    bool changed = false;
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
                        if (isSelected)
                          ImGui::SetItemDefaultFocus();
                      }
                      ImGui::EndCombo();
                    }
                    return changed;
                  }))
    {
      hasChanged = true;
    }

    // ---- Output buffer selection ----
    PE::entry("Output",
              [&]()
              {
                int current = static_cast<int>(m_currentOutput);
                if (ImGui::BeginCombo("##output", kOutputNames[current]))
                {
                  for (int i = 0; i < RenderOutput::Count; ++i)
                  {
                    bool isSelected = (current == i);
                    if (ImGui::Selectable(kOutputNames[i], isSelected))
                    {
                      m_currentOutput = static_cast<RenderOutput>(i);
                    }
                    if (isSelected)
                      ImGui::SetItemDefaultFocus();
                  }
                  ImGui::EndCombo();
                }
                return false;
              });

    // ---- Mode-specific parameters ----
    if (currentMode == "Raytrace")
    {
      shaderio::RenderParams& params = renderer->renderParams();
      hasChanged |= PE::DragInt("Samples", &params.nSamples, 1.0F, 0, 1024);
      hasChanged |=
          PE::DragInt("Max Bounces", &params.maxBounces, 1.0F, 0, 1024);
      bool denoiseEnabled = renderer->denoise();
      if (PE::Checkbox("Denoise", &denoiseEnabled))
      {
        renderer->setDenoise(denoiseEnabled);
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
      if (PE::Checkbox("Enable Shadows", (bool*) &params.enableShadows))
      {
        hasChanged = true;
      }
    }

    PE::end();
  }

  return hasChanged;
}

}  // namespace app
