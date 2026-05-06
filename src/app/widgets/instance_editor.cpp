#include "instance_editor.hpp"

#include <algorithm>
#include <unordered_map>

#include <fmt/format.h>
#include <imgui.h>

#include "core/math.hpp"
#include "glm/gtc/type_ptr.hpp"
#include "property_editor.hpp"
#include "shaders/shared/structs.h"

namespace app
{

/**********************************************************/
bool InstanceEditor::render(
    SceneResourcesManager&                       resources,
    const std::map<MaterialType, MaterialEntry>& shaderRegistry)
/**********************************************************/
{
  namespace PE        = app::PropertyEditor;
  auto&       instances   = resources.getInstances();
  const auto& materials   = resources.getMaterials();
  const auto& instanceMap = resources.instanceMap();
  const auto& meshes      = resources.getMeshes();
  const auto& meshMap     = resources.meshMap();
  const auto& materialMap = resources.materialMap();

  std::string                       matNamesList;
  std::vector<MaterialID>           matIDs;
  std::unordered_map<uint32_t, int> matIDToIndex;  // <MaterialID, ComboIndex>

  int counter = 0;
  for (auto const& [matName, mId] : materialMap)
  {
    matNamesList += matName + '\0';
    matIDs.push_back(mId);
    matIDToIndex[mId] = counter++;
  }
  matNamesList += '\0';

  bool changed = false;
  if (ImGui::CollapsingHeader("Instances", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::InputTextWithHint("##InstSearch", "Search instances...", m_search,
                             IM_ARRAYSIZE(m_search));

    ImGui::SameLine();
    if (ImGui::Button("+ Add"))
    {
      shaderio::Instance newInst;
      newInst.transform     = math::composeTransform(newInst.translation,
                                                 newInst.rotation, newInst.scale);
      newInst.materialIndex = 0;
      newInst.meshIndex     = 0;
      newInst.hit_group     = MaterialType::eDiffuse;

      resources.addInstance(std::move(newInst));
      changed = true;
    }
    ImGui::Separator();

    std::string searchStr = m_search;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    for (const auto& [name, id] : instanceMap)
    {
      if (id >= instances.size())
        continue;

      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
        continue;

      std::string label = fmt::format("{}[{}]##{}", name, id, id);
      if (ImGui::TreeNode(label.c_str()))
      {
        auto& inst  = instances[id];
        int   matIdx = static_cast<int>(inst.materialIndex);
        PE::begin();

        int currentComboItem = -1;
        if (auto it = matIDToIndex.find(inst.materialIndex);
            it != matIDToIndex.end())
        {
          currentComboItem = it->second;
        }

        if (PE::Combo("Material Select", &currentComboItem, matNamesList.c_str(),
                      (int)matIDs.size()))
        {
          inst.materialIndex = matIDs[currentComboItem];
          matIdx             = (int)inst.materialIndex;
          changed            = true;
        }
        if (PE::SliderInt("Material ID", &matIdx, 0,
                          (int)materials.size() - 1))
        {
          inst.materialIndex = (uint32_t)matIdx;
          changed            = true;
        }

        // Hit Group (Shader) Assignment
        std::vector<MaterialType> types;
        std::string               shaderNames;
        int                       currentTypeIdx = -1;
        int                       count          = 0;
        for (auto const& [type, entry] : shaderRegistry)
        {
          if (type == inst.hit_group)
            currentTypeIdx = count;
          shaderNames += entry.prettyName + '\0';
          types.push_back(type);
          count++;
        }
        shaderNames += '\0';

        if (PE::Combo("Shader Type", &currentTypeIdx, shaderNames.c_str(),
                      (int)types.size()))
        {
          inst.hit_group = types[currentTypeIdx];
          changed        = true;
        }

        // Mesh index
        std::string currentMeshName = "Unknown";
        for (const auto& [meshName, meshId] : meshMap)
        {
          if (meshId == inst.meshIndex)
          {
            currentMeshName = meshName;
            break;
          }
        }
        PE::Text("Mesh Name", currentMeshName.c_str());
        int currentMeshIdx = static_cast<int>(inst.meshIndex);
        if (PE::SliderInt("Mesh ID", &currentMeshIdx, 0,
                          std::max(0, (int)meshes.size() - 1)))
        {
          inst.meshIndex = static_cast<uint32_t>(currentMeshIdx);
          changed        = true;
        }

        glm::quat rotation      = math::toQuat(glm::vec4(inst.rotation));
        glm::vec3 rotationEuler = glm::degrees(glm::eulerAngles(rotation));

        bool tChanged =
            PE::DragFloat3("Position", glm::value_ptr(inst.translation), 0.1f);
        bool rChanged =
            PE::DragFloat3("Rotation", glm::value_ptr(rotationEuler), 0.5f);
        bool sChanged =
            PE::DragFloat3("Scale", glm::value_ptr(inst.scale), 0.05f);

        if (tChanged || rChanged || sChanged)
        {
          glm::quat quat = glm::quat(glm::radians(rotationEuler));
          inst.rotation  = math::fromQuat(quat);
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

}  // namespace app
