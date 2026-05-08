#include "light_editor.hpp"

#include <cstdio>

#include <glm/ext/quaternion_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "core/image.hpp"
#include "property_editor.hpp"
#include "renderer/interfaces/device_assets_interface.hpp"
#include "shaders/shared/structs.h"

namespace app
{

/**********************************************************/
LightChangedBitMask
LightEditor::render(scene::SceneResourcesManager& resources,
                    const std::shared_ptr<IDeviceAssets>& deviceResources)
/**********************************************************/
{
  shaderio::SceneInfo& sceneInfo = resources.sceneInfo();
  namespace PE = app;

  // Initialize as None (0)
  uint32_t mask = LightChangedBitMask::NoneChanged;

  // Environment Type Selection (Radio Buttons)
  if (PE::begin("EnvTypeTable"))
  {
    if (PE::RadioButton("Physical Sky", sceneInfo.useSky))
    {
      sceneInfo.useSky = true;
      sceneInfo.useEnv = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    if (sceneInfo.envmapLight.totalSum > 0 &&
        PE::RadioButton("HDR Environment", sceneInfo.useEnv))
    {
      sceneInfo.useEnv = true;
      sceneInfo.useSky = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    bool useSolid = !sceneInfo.useSky && !sceneInfo.useEnv;
    if (PE::RadioButton("Solid Color", useSolid))
    {
      sceneInfo.useSky = false;
      sceneInfo.useEnv = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    PE::end();
  }

  ImGui::Separator();

  // Specific Settings based on selection
  if (sceneInfo.useSky)
  {
    if (m_skyEditor.renderSimpleParameters(sceneInfo.skySimpleParam))
    {
      mask |= LightChangedBitMask::EnvmapChanged;
    }
  }
  else if (sceneInfo.useEnv)
  {
    auto& env = sceneInfo.envmapLight;
    if (PE::begin("EnvParamsTable"))
    {
      const core::Image& envmapImage = resources.getEnvmap();
      TextureID textureToDelete = -1;

      // --- PREVIEW SECTION ---
      PE::entry("Preview",
                [&]()
                {
                  ImTextureID gpuHandle =
                      deviceResources->getTextureHandle(env.envTextureIdx);
                  if (gpuHandle)
                  {
                    float ratio = (env.dims.x > 0)
                                      ? (float) env.dims.y / (float) env.dims.x
                                      : 0.5f;
                    float width =
                        std::min(ImGui::GetContentRegionAvail().x, 300.0f);
                    ImGui::Image(gpuHandle, ImVec2(width, width * ratio),
                                 {0, 0}, {1, 1}, {1, 1, 1, 1}, {0, 0, 0, 0.3f});
                  }
                  else
                  {
                    ImGui::TextColored({1, 0.3f, 0.3f, 1}, "No GPU Texture");
                  }
                  return false;
                });

      // --- PARAMETERS ---
      if (PE::DragFloat("Intensity", &env.scale, 0.1f, 0.0f, 10.0f))
      {
        mask |= LightChangedBitMask::EnvmapChanged;
      }

      if (PE::DragFloat("Rotation (Azimuth)", &env.rotationAzimuthDegree, 1.0f,
                        0.0f, 360.0f))
      {
        env.rotation = glm::rotate(glm::mat4(1.0f),
                                   glm::radians(env.rotationAzimuthDegree),
                                   glm::vec3(0, 1, 0));
        mask |= LightChangedBitMask::EnvmapChanged;
      }

      // --- TECHNICAL INFO ---
      if (PE::treeNode("Technical Info"))
      {
        PE::Text("Resolution", "%u x %u", env.dims.x, env.dims.y);
        PE::Text("Tex Index", "%d", env.envTextureIdx);
        PE::Text("Integral", "%.4f", env.totalSum);
        PE::Text("Format", "%s", core::formatToString(envmapImage.format));
        PE::treePop();
      }

      // --- ACTIONS ---
      if (PE::Button("Reload Map", ImVec2(-1, 0)))
      {
        mask |= LightChangedBitMask::EnvmapChanged;
      }

      PE::entry(
          "Management",
          [&]()
          {
            ImGui::PushStyleColor(ImGuiCol_Button, {0.5f, 0.1f, 0.1f, 1.0f});
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  {0.7f, 0.1f, 0.1f, 1.0f});
            if (ImGui::Button("Delete Environment Map", ImVec2(-FLT_MIN, 0)))
            {
              textureToDelete = env.envTextureIdx;
            }
            ImGui::PopStyleColor(2);
            return false;
          });

      PE::end();

      // Handle deletion after PE::end to avoid modifying resources mid-layout
      if (textureToDelete != -1)
      {
        if (!resources.destroyTexture(textureToDelete))
        {
          printf("Failed to destroy environment map texture: %d\n",
                 textureToDelete);
        }
        else
        {
          env.totalSum = 0;
          sceneInfo.useEnv = false;
          sceneInfo.useSky = true;
        }
        mask |= LightChangedBitMask::EnvmapChanged;
      }
    }
  }
  else
  {
    if (PE::begin("SolidColorTable"))
    {
      if (PE::ColorEdit3("Background Color",
                         (float*) &sceneInfo.backgroundColor))
      {
        mask |= LightChangedBitMask::EnvmapChanged;
      }
      PE::end();
    }
  }

  // Punctual Light Section
  if (ImGui::TreeNodeEx("Punctual Light", ImGuiTreeNodeFlags_DefaultOpen))
  {
    auto& light = sceneInfo.punctualLights[0];
    if (PE::begin("LightTable"))
    {
      int typeInt = static_cast<int>(light.type);
      if (PE::Combo("Type", &typeInt, "Point\0Spot\0Directional\0"))
      {
        light.type = (shaderio::LightType) typeInt;
        mask |= LightChangedBitMask::PunctualLightChanged;
      }

      if (light.type != shaderio::LightType::eDirectional)
        if (PE::DragFloat3("Position", glm::value_ptr(light.position), 0.1f))
          mask |= LightChangedBitMask::PunctualLightChanged;

      if (light.type != shaderio::LightType::ePoint)
        if (PE::SliderFloat3("Direction", glm::value_ptr(light.direction),
                             -1.0f, 1.0f))
          mask |= LightChangedBitMask::PunctualLightChanged;

      if (PE::DragFloat("Intensity", &light.intensity, 1.0f, 0.0f, 10000.0f,
                        "%.2f"))
      {
        mask |= LightChangedBitMask::PunctualLightChanged;
      }
      if (PE::ColorEdit3("Color", glm::value_ptr(light.color)))
        mask |= LightChangedBitMask::PunctualLightChanged;

      if (light.type == shaderio::LightType::eSpot)
        if (PE::SliderAngle("Cone Angle", &light.coneAngle, 0.0f, 90.0f))
          mask |= LightChangedBitMask::PunctualLightChanged;
      if (PE::Button("Reload", ImVec2(-1, 0)))
      {
        mask |= LightChangedBitMask::All;
      }

      PE::end();
    }
    ImGui::TreePop();
  }

  return static_cast<LightChangedBitMask>(mask);
}

}  // namespace app
