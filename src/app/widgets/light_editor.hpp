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
inline bool lightEditor(SceneResourcesManager &resources)
/**********************************************************/
{
  shaderio::SceneInfo &sceneInfo = resources.sceneInfo();
  namespace PE = app::PropertyEditor;
  bool hasChanged = false;
  // Environment Type Selection (Radio Buttons)
  if (PE::begin("EnvTypeTable")) {
    if (PE::RadioButton("Physical Sky", sceneInfo.useSky)) {
      sceneInfo.useSky = true;
      sceneInfo.useEnv = false;
      hasChanged = true;
    }
    if (sceneInfo.envmapLight.totalSum > 0 &&
        PE::RadioButton("HDR Environment", sceneInfo.useEnv)) {
      sceneInfo.useEnv = true;
      sceneInfo.useSky = false;
      hasChanged = true;
    }
    bool useSolid = !sceneInfo.useSky && !sceneInfo.useEnv;
    if (PE::RadioButton("Solid Color", useSolid)) {
      sceneInfo.useSky = false;
      sceneInfo.useEnv = false;
      hasChanged = true;
    }
    PE::end();
  }

  ImGui::Separator();

  // Specific Settings based on selection
  if (sceneInfo.useSky) {
    hasChanged |= app::skySimpleParametersUI(sceneInfo.skySimpleParam);
  } else if (sceneInfo.useEnv) {
    auto &env = sceneInfo.envmapLight;
    if (PE::begin("EnvParamsTable")) {
      hasChanged |= PE::DragFloat("Intensity", &env.scale, 0.1f, 0.0f, 10.0f);
      if ((hasChanged |=
           PE::DragFloat("Rotation (Azimuth)", &env.rotationAzimuthDegree, 1.0f,
                         0.0f, 360.0f))) {
        env.rotation = glm::rotate(glm::mat4(1.0f),
                                   glm::radians(env.rotationAzimuthDegree),
                                   glm::vec3(0, 1, 0));
      }

      if (PE::treeNode("Technical Info")) {
        PE::Text("Resolution", "%u x %u", env.dims.x, env.dims.y);
        PE::Text("Tex Index", "%d", env.envTextureIdx);
        PE::Text("Integral", "%.4f", env.totalSum);
        PE::treePop();
      }

      if (PE::Button("Reload Map",
                     ImVec2(-1, 0))) { /* m_sceneManager.reloadEnvmap(); */
      }
      PE::end();
    }
  } else {
    if (PE::begin("SolidColorTable")) {
      hasChanged |= PE::ColorEdit3("Background Color",
                                   (float *)&sceneInfo.backgroundColor);
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
        hasChanged = true;
      }

      if (light.type != shaderio::LightType::eDirectional)
        hasChanged |=
            PE::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
      if (light.type != shaderio::LightType::ePoint)
        hasChanged |= PE::SliderFloat3(
            "Direction", glm::value_ptr(light.direction), -1.0f, 1.0f);

      hasChanged |=
          PE::DragFloat("Intensity", &light.intensity, 1.0f, 0.0f, 10000.0f,
                        "%.2f", ImGuiSliderFlags_Logarithmic);
      hasChanged |= PE::ColorEdit3("Color", glm::value_ptr(light.color));

      if (light.type == shaderio::LightType::eSpot)
        hasChanged |=
            PE::SliderAngle("Cone Angle", &light.coneAngle, 0.0f, 90.0f);

      PE::end();
    }
    ImGui::TreePop();
  }

  return hasChanged;
}
} // namespace app
