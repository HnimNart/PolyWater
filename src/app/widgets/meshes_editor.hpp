#pragma once

#include <algorithm>

#include "property_editor.hpp"
#include "scene/scene_resources.hpp"
#include "shaders/shared/structs.h"

namespace app
{

/**********************************************************/
inline bool meshEditor(SceneResourcesManager& resources)
/**********************************************************/
{
  namespace PE = app::PropertyEditor;
  auto& meshes = resources.getMeshes();
  const auto& meshMap = resources.meshMap();

  static char meshSearch[128] = "";

  if (ImGui::CollapsingHeader("Meshes", ImGuiTreeNodeFlags_DefaultOpen))
  {
    ImGui::InputTextWithHint("##MeshSearch", "Search meshes...", meshSearch,
                             IM_ARRAYSIZE(meshSearch));
    ImGui::Separator();

    std::string searchStr = meshSearch;
    std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                   ::tolower);

    for (const auto& [name, id] : meshMap)
    {
      if (id >= meshes.size())
        continue;

      // Filter logic
      std::string nameLower = name;
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     ::tolower);
      if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos)
        continue;

      std::string label = fmt::format("{}##mesh_{}", name, id);

      // 1. Root Mesh Node
      if (ImGui::TreeNode(label.c_str()))
      {
        const auto& mesh = meshes[id];
        PE::begin();
        PE::Text("Mesh ID", fmt::format("{}", id).c_str());
        PE::Text("Vertices",
                 fmt::format("{}", mesh.triMesh.positions.count).c_str());
        PE::Text("Indices",
                 fmt::format("{}", mesh.triMesh.indices.count).c_str());
        const shaderio::BoundingBox& bbox = mesh.bbox;
        PE::Text("BBox Min", fmt::format("{:.1f}, {:.1f}, {:.1f}", bbox.min.x,
                                         bbox.min.y, bbox.min.z)
                                 .c_str());
        PE::Text("BBox Max", fmt::format("{:.1f}, {:.1f}, {:.1f}", bbox.max.x,
                                         bbox.max.y, bbox.max.z)
                                 .c_str());
        PE::end();

        ImGui::TreePop();
      }
    }
  }
  return false;
}

}  // namespace app
