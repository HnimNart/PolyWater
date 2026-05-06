#include "material_editor.hpp"

#include <fmt/format.h>
#include <imgui.h>

#include <algorithm>

#include "glm/gtc/type_ptr.hpp"
#include "property_editor.hpp"
#include "shaders/shared/structs.h"

namespace app
{

/**********************************************************/
bool MaterialEditor::render(SceneResourcesManager& resources)
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto& materials = resources.getMaterials();
  const auto& materialMap = resources.materialMap();
  bool changed = false;

  if (ImGui::CollapsingHeader("Materials", ImGuiTreeNodeFlags_DefaultOpen))
  {
    // 1. Search Bar
    ImGui::InputTextWithHint("##MatSearch", "Filter by name...", m_search,
                             IM_ARRAYSIZE(m_search));
    ImGui::SameLine();

    if (ImGui::Button("+ Add"))
    {
      shaderio::Material newMat{};
      newMat.baseColorFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);  // Light grey
      newMat.metallicFactor = 0.0f;
      newMat.roughnessFactor = 0.5f;
      newMat.emission = glm::vec3(0.0f);
      newMat.ior = glm::vec3(1.5f);  // Standard glass/plastic IOR
      newMat.sigma_t = glm::vec3(0.0f);
      newMat.asymmetry = glm::vec3(0.0f);
      newMat.baseColorTextureIndex = -1;

      resources.addMaterial(std::move(newMat));
      changed = true;
    }
    ImGui::Separator();

    // Prepare search string for case-insensitive comparison
    std::string searchStr = m_search;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    // 2. Iterate through the map (already alphabetical)
    for (const auto& [name, id] : materialMap)
    {
      if (id >= materials.size())
        continue;

      // Apply Search Filter
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
      {
        continue;
      }

      std::string label = fmt::format("{} (ID: {})", name, id);

      if (ImGui::TreeNode(label.c_str()))
      {
        auto& mat = materials[id];
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

        const auto& textureMap = resources.textureMap();
        std::string currentName = "None";
        for (const auto& [texName, texId] : textureMap)
        {
          if (texId == mat.baseColorTextureIndex)
          {
            currentName = texName;
            break;
          }
        }

        PE::entry(
            "Base Color Texture",
            [&]()
            {
              bool itemChanged = false;
              if (ImGui::BeginCombo("##BaseColorTex", currentName.c_str()))
              {
                for (const auto& [texName, texId] : textureMap)
                {
                  const bool isSelected = (currentName == texName);

                  if (ImGui::Selectable(texName.c_str(), isSelected))
                  {
                    mat.baseColorTextureIndex = texId;
                    itemChanged = true;
                    changed = true;
                    resources.onMaterialChange();
                  }

                  if (isSelected)
                  {
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

}  // namespace app
