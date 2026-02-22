#pragma once

#include <glm/glm.hpp>

#include <glm/ext/quaternion_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "property_editor.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"
#include "sky.hpp"

namespace app {

/**********************************************************/
inline LightChangedBitMask lightEditor(SceneResourcesManager &resources)
/**********************************************************/
{
  shaderio::SceneInfo &sceneInfo = resources.sceneInfo();
  namespace PE = app::PropertyEditor;

  // Initialize as None (0)
  uint32_t mask = LightChangedBitMask::NoneChanged;

  // Environment Type Selection (Radio Buttons)
  if (PE::begin("EnvTypeTable")) {
    if (PE::RadioButton("Physical Sky", sceneInfo.useSky)) {
      sceneInfo.useSky = true;
      sceneInfo.useEnv = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    if (sceneInfo.envmapLight.totalSum > 0 &&
        PE::RadioButton("HDR Environment", sceneInfo.useEnv)) {
      sceneInfo.useEnv = true;
      sceneInfo.useSky = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    bool useSolid = !sceneInfo.useSky && !sceneInfo.useEnv;
    if (PE::RadioButton("Solid Color", useSolid)) {
      sceneInfo.useSky = false;
      sceneInfo.useEnv = false;
      mask |= LightChangedBitMask::EnvmapChanged;
    }
    PE::end();
  }

  ImGui::Separator();

  // Specific Settings based on selection
  if (sceneInfo.useSky) {
    if (app::skySimpleParametersUI(sceneInfo.skySimpleParam)) {
      mask |= LightChangedBitMask::EnvmapChanged;
    }
  } else if (sceneInfo.useEnv) {
    auto &env = sceneInfo.envmapLight;
    if (PE::begin("EnvParamsTable")) {
      if (PE::DragFloat("Intensity", &env.scale, 0.1f, 0.0f, 10.0f))
        mask |= LightChangedBitMask::EnvmapChanged;

      if (PE::DragFloat("Rotation (Azimuth)", &env.rotationAzimuthDegree, 1.0f,
                        0.0f, 360.0f)) {
        env.rotation = glm::rotate(glm::mat4(1.0f),
                                   glm::radians(env.rotationAzimuthDegree),
                                   glm::vec3(0, 1, 0));
        mask |= LightChangedBitMask::EnvmapChanged;
      }

      if (PE::treeNode("Technical Info")) {
        PE::Text("Resolution", "%u x %u", env.dims.x, env.dims.y);
        PE::Text("Tex Index", "%d", env.envTextureIdx);
        PE::Text("Integral", "%.4f", env.totalSum);
        PE::treePop();
      }

      if (PE::Button("Reload Map", ImVec2(-1, 0))) {
        mask |= LightChangedBitMask::EnvmapChanged;
      }

      PE::end();
    }
  } else {
    if (PE::begin("SolidColorTable")) {
      if (PE::ColorEdit3("Background Color",
                         (float *)&sceneInfo.backgroundColor)) {
        mask |= LightChangedBitMask::EnvmapChanged;
      }
      PE::end();
    }
  }

  // Punctual Light Section
  if (ImGui::TreeNodeEx("Punctual Light", ImGuiTreeNodeFlags_DefaultOpen)) {
    auto &light = sceneInfo.punctualLights[0];
    if (PE::begin("LightTable")) {
      int typeInt = static_cast<int>(light.type);
      if (PE::Combo("Type", &typeInt, "Point\0Spot\0Directional\0")) {
        light.type = (shaderio::LightType)typeInt;
        mask |=
            LightChangedBitMask::PunctualLightChanged; // Assuming punctual
                                                       // lights fall under the
                                                       // AreaLight update path
      }

      if (light.type != shaderio::LightType::eDirectional)
        if (PE::DragFloat3("Position", glm::value_ptr(light.position), 0.1f))
          mask |= LightChangedBitMask::PunctualLightChanged;

      if (light.type != shaderio::LightType::ePoint)
        if (PE::SliderFloat3("Direction", glm::value_ptr(light.direction),
                             -1.0f, 1.0f))
          mask |= LightChangedBitMask::PunctualLightChanged;

      if (PE::DragFloat("Intensity", &light.intensity, 1.0f, 0.0f, 10000.0f,
                        "%.2f", ImGuiSliderFlags_Logarithmic))
        mask |= PunctualLightChanged;
      if (PE::ColorEdit3("Color", glm::value_ptr(light.color)))
        mask |= LightChangedBitMask::PunctualLightChanged;

      if (light.type == shaderio::LightType::eSpot)
        if (PE::SliderAngle("Cone Angle", &light.coneAngle, 0.0f, 90.0f))
          mask |= LightChangedBitMask::PunctualLightChanged;
      if (PE::Button("Reload", ImVec2(-1, 0))) {
        mask |= LightChangedBitMask::All;
      }

      PE::end();
    }
    ImGui::TreePop();
  }

  return static_cast<LightChangedBitMask>(mask);
}

} // namespace app
