#pragma once

#include <algorithm>

#include "core/Math.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "property_editor.hpp"
#include "renderer/interfaces/IRenderer.hpp"
#include "scene/SceneResources.hpp"
#include "shaders/shared/structs.h"

namespace app {
/**********************************************************/
bool instanceEditor(SceneResourcesManager &resources,
                    const std::map<MaterialType, MaterialEntry> &shaderRegistry)
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto &instances = resources.getInstances();
  const auto &materials = resources.getMaterials();
  const auto &instanceMap = resources.instanceMap();
  const auto &meshes = resources.getMeshes();
  const auto &meshMap = resources.meshMap();
  const auto &materialMap = resources.materialMap();

  std::string m_matNamesList;
  std::vector<MaterialID> m_matIDs;
  std::unordered_map<uint32_t, int> m_matIDToIndex; // <MaterialID, ComboIndex>

  int counter = 0;
  for (auto const &[matName, mId] : materialMap) {
    m_matNamesList += matName + '\0';
    m_matIDs.push_back(mId);
    m_matIDToIndex[mId] = counter++;
  }
  m_matNamesList += '\0';

  bool changed = false;
  static char instanceSearch[128] = "";
  if (ImGui::CollapsingHeader("Instances", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::InputTextWithHint("##InstSearch", "Search instances...",
                             instanceSearch, IM_ARRAYSIZE(instanceSearch));

    ImGui::SameLine();
    if (ImGui::Button("+ Add")) { // Add new instance
      shaderio::Instance newInst;

      newInst.transform = math::composeTransform(
          newInst.translation, newInst.rotation, newInst.scale);
      newInst.materialIndex = 0;
      newInst.meshIndex = 0;
      newInst.hit_group = static_cast<uint32_t>(MaterialType::eDiffuse);

      auto newId = resources.addInstance(std::move(newInst));
      changed = true;
    }
    ImGui::Separator();

    std::string searchStr = instanceSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    // Iterate through the map
    for (const auto &[name, id] : instanceMap) {
      if (id >= instances.size())
        continue;

      // Filter
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
        continue;

      std::string label = fmt::format("{}[{}]##{}", name, id, id);
      if (ImGui::TreeNode(label.c_str())) {
        auto &inst = instances[id];
        int matIdx = static_cast<int>(inst.materialIndex);
        PE::begin();

        // Find current id of the selected material
        int currentComboItem = -1;
        if (auto it = m_matIDToIndex.find(inst.materialIndex);
            it != m_matIDToIndex.end()) {
          currentComboItem = it->second;
        }

        // Draw the Dropdown
        if (PE::Combo("Material Select", &currentComboItem,
                      m_matNamesList.c_str(), (int)m_matIDs.size())) {
          inst.materialIndex = m_matIDs[currentComboItem];
          matIdx = (int)inst.materialIndex; // Sync slider
          changed = true;
        }
        // Draw the Slider
        if (PE::SliderInt("Material ID", &matIdx, 0,
                          (int)materials.size() - 1)) {
          inst.materialIndex = (uint32_t)matIdx;
          changed = true;
        }

        // Hit Group (Shader) Assignment
        std::vector<MaterialType> types;
        std::string shaderNames;
        int currentTypeIdx = -1;
        int count = 0;
        for (auto const &[type, entry] : shaderRegistry) {
          if (type == inst.hit_group)
            currentTypeIdx = count;
          shaderNames += entry.prettyName + '\0';
          types.push_back(type);
          count++;
        }
        shaderNames += '\0';

        if (PE::Combo("Shader Type", &currentTypeIdx, shaderNames.c_str(),
                      (int)types.size())) {
          inst.hit_group = static_cast<uint32_t>(types[currentTypeIdx]);
          changed = true;
        }

        // Mesh index
        std::string currentMeshName = "Unknown";
        // Reverse lookup: Find the string key that matches our current ID
        for (const auto &[name, id] : meshMap) {
          if (id == inst.meshIndex) {
            currentMeshName = name;
            break;
          }
        }
        PE::Text("Mesh Name", currentMeshName.c_str());
        int currentMeshIdx = static_cast<int>(inst.meshIndex);
        if (PE::SliderInt("Mesh ID", &currentMeshIdx, 0,
                          std::max(0, (int)meshes.size() - 1))) {
          inst.meshIndex = static_cast<uint32_t>(currentMeshIdx);
          changed = true;
        }

        glm::quat rotation = math::toQuat(glm::vec4(inst.rotation));
        glm::vec3 rotationEuler = glm::degrees(glm::eulerAngles(rotation));

        bool tChanged =
            PE::DragFloat3("Position", glm::value_ptr(inst.translation), 0.1f);
        bool rChanged =
            PE::DragFloat3("Rotation", glm::value_ptr(rotationEuler), 0.5f);
        bool sChanged =
            PE::DragFloat3("Scale", glm::value_ptr(inst.scale), 0.05f);

        if (tChanged || rChanged || sChanged) {
          glm::quat quat = glm::quat(glm::radians(rotationEuler));
          inst.rotation = math::fromQuat(quat);
          inst.transform = math::composeTransform(inst.translation,
                                                  inst.rotation, inst.scale);
          changed = true;
        }

        PE::end();
        ImGui::TreePop();
      }
    }
  }

  return changed;
}

} // namespace app
