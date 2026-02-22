#pragma once

#include <algorithm>

#include "glm/gtc/type_ptr.hpp"
#include "property_editor.hpp"

#include "scene/SceneResources.hpp"

namespace app {

/**********************************************************/
inline bool materialEditor(SceneResourcesManager &resources)
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto &materials = resources.getMaterials();
  const auto &materialMap = resources.materialMap();
  bool changed = false;

  // Static buffer to persist search text between frames
  static char materialSearch[128] = "";

  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen)) {
    // 1. Search Bar
    ImGui::InputTextWithHint("##MatSearch", "Filter by name...", materialSearch,
                             IM_ARRAYSIZE(materialSearch));
    ImGui::SameLine();

    if (ImGui::Button("+ Add")) {

      shaderio::Material newMat{};
      newMat.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f); // Light grey
      newMat.metallicFactor = 0.0f;
      newMat.roughnessFactor = 0.5f;
      newMat.emission = glm::vec3(0.0f);
      newMat.ior = glm::vec3(1.5f); // Standard glass/plastic IOR
      newMat.sigma_t = glm::vec3(0.0f);
      newMat.asymmetry = glm::vec3(0.0f);
      newMat.baseColorTextureIndex = -1; // Assuming -1 or 0 means "no texture"

      resources.addMaterial(std::move(newMat));
      changed = true;
    }
    ImGui::Separator();

    // Prepare search string for case-insensitive comparison
    std::string searchStr = materialSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    // 2. Iterate through the map (already alphabetical)
    for (const auto &[name, id] : materialMap) {
      if (id >= materials.size())
        continue;

      // Apply Search Filter
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() &&
          nameLower.find(searchStr) == std::string::npos) {
        continue;
      }

      // Use name and ID for a unique ImGui ID
      std::string label = fmt::format("{} (ID: {})", name, id);

      if (ImGui::TreeNode(label.c_str())) {
        auto &mat = materials[id];
        PE::begin();

        changed |=
            PE::ColorEdit4("Base Color", glm::value_ptr(mat.baseColorFactor));
        changed |=
            PE::SliderFloat("Metallic", &mat.metallicFactor, 1e-4f, 1.0f);
        changed |=
            PE::SliderFloat("Roughness", &mat.roughnessFactor, 1e-3f, 1.0f);
        changed |= PE::SliderFloat3("Emission", glm::value_ptr(mat.emission),
                                    0.0F, 100.F);
        changed |= PE::SliderFloat3("IOR (Spectral)", glm::value_ptr(mat.ior),
                                    1.0f, 2.5f);
        changed |= PE::SliderFloat3("Extinction", glm::value_ptr(mat.sigma_t),
                                    0.0f, 100.0f);
        changed |= PE::SliderFloat3("Asymmetry", glm::value_ptr(mat.asymmetry),
                                    0.0f, 1.0f);

        const auto &textureMap = resources.textureMap();
        std::string currentName = "None";
        for (const auto &[name, id] : textureMap) {
          if (id == mat.baseColorTextureIndex) {
            currentName = name;
            break;
          }
        }

        PE::entry("Base Color Texture", [&]() {
          bool itemChanged = false;
          if (ImGui::BeginCombo("##BaseColorTex", currentName.c_str())) {
            for (const auto &[name, id] : textureMap) {
              const bool isSelected = (currentName == name);

              if (ImGui::Selectable(name.c_str(), isSelected)) {
                mat.baseColorTextureIndex = id;
                itemChanged = true;
                changed = true;
                resources.onMaterialChange();
              }

              if (isSelected) {
                ImGui::SetItemDefaultFocus();
              }
            }
            ImGui::EndCombo();
          }
          return itemChanged;
        });

        PE::end();
        ImGui::TreePop();
      }
    }
  }
  return changed;
}

} // namespace app
