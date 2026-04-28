#pragma once

#include <imgui.h>

#include <algorithm>

#include "core/string_utils.hpp"
#include "renderer/interfaces/renderer_interface.hpp"
#include "scene/scene_resources.hpp"
#include "tooltip.hpp"

namespace app
{

/**********************************************************/
template <typename ImageType>
void renderTextureItem(const std::string& name, const ImageType& image,
                       const std::string& searchStr,
                       const std::shared_ptr<IDeviceAssets>& deviceResources,
                       TextureID& textureToDelete)
/**********************************************************/
{
  if (!image.isValid())
    return;

  // Filtering by name or filename
  std::string nameLower = name;
  std::string fileLower = image.filename;
  std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                 ::tolower);
  std::transform(fileLower.begin(), fileLower.end(), fileLower.begin(),
                 ::tolower);

  if (!searchStr.empty() && nameLower.find(searchStr) == std::string::npos &&
      fileLower.find(searchStr) == std::string::npos)
  {
    return;
  }

  if (ImGui::TreeNode(name.c_str()))
  {
    // --- HEADER INFO ---
    std::string fileName = core::getFileName(image.filename);
    ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Source: %s",
                       fileName.c_str());
    app::tooltip(image.filename.c_str(), true, 0.5f);
    ImGui::Dummy(ImVec2(0.0f, 2.0f));

    // --- OUTER LAYOUT TABLE ---
    if (ImGui::BeginTable("##ImageAndMeta", 2,
                          ImGuiTableFlags_SizingStretchProp))
    {
      ImGui::TableNextRow();

      // --- LEFT COLUMN: IMAGE PREVIEW ---
      ImGui::TableNextColumn();
      ImTextureID gpuHandle =
          deviceResources->getTextureHandle(image.textureId);

      if (gpuHandle)
      {
        float availWidth = ImGui::GetContentRegionAvail().x;
        float ratio = (image.width > 0)
                          ? (float) image.height / (float) image.width
                          : 1.0f;

        float displayWidth = std::min(availWidth * 0.9f, 200.0f);
        ImVec2 displaySize = ImVec2(displayWidth, displayWidth * ratio);

        ImGui::Image(gpuHandle, displaySize, ImVec2(0, 0), ImVec2(1, 1),
                     ImVec4(1, 1, 1, 1), ImVec4(1, 1, 1, 0.3f));
      }
      else
      {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Invalid GPU State");
      }

      // --- RIGHT COLUMN: METADATA ---
      ImGui::TableNextColumn();

      if (ImGui::BeginTable("##TexSpecs", 2, ImGuiTableFlags_SizingFixedFit))
      {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Resolution:");
        ImGui::TableNextColumn();
        ImGui::Text("%u x %u", image.width, image.height);

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("Format:");
        ImGui::TableNextColumn();
        ImGui::Text("%s", core::formatToString(image.format));

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextDisabled("ID:");
        ImGui::TableNextColumn();
        ImGui::Text("%ld", image.textureId);

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.5f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                              ImVec4(0.7f, 0.1f, 0.1f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                              ImVec4(0.9f, 0.1f, 0.1f, 1.0f));

        if (ImGui::Button(("Delete ##" + name).c_str(), ImVec2(-FLT_MIN, 0)))
        {
          textureToDelete = image.textureId;
        }
        ImGui::PopStyleColor(3);

        ImGui::EndTable();
      }

      ImGui::EndTable();  // End Outer Table
    }

    ImGui::TreePop();
  }
}

/**********************************************************/
bool textureEditor(SceneResourcesManager& resourceManager,
                   const std::shared_ptr<IDeviceAssets>& deviceResources)
/**********************************************************/
{

  const auto& textureMap = resourceManager.textureImageMap();

  static char filter[128] = "";
  ImGui::InputTextWithHint("##Filter", "Filter textures...", filter,
                           IM_ARRAYSIZE(filter));
  ImGui::Separator();

  std::string searchStr = filter;
  std::transform(searchStr.begin(), searchStr.end(), searchStr.begin(),
                 ::tolower);

  TextureID textureToDelete = -1;

  for (const auto& [name, image] : textureMap)
  {
    renderTextureItem(name, image, searchStr, deviceResources, textureToDelete);
  }

  if (textureToDelete != -1)
  {
    if (!resourceManager.destroyTexture(textureToDelete))
    {
      printf("Failed to destroy texture %d\n", textureToDelete);
    }
  }
  return false;
}

}  // namespace app
