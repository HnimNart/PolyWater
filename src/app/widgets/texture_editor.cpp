#include "texture_editor.hpp"

#include <algorithm>
#include <cstdio>

namespace app
{

/**********************************************************/
bool TextureEditor::render(
    scene::SceneResourcesManager& resourceManager,
    const std::shared_ptr<IDeviceAssets>& deviceResources)
/**********************************************************/
{
  const auto& textureMap = resourceManager.textureImageMap();

  ImGui::InputTextWithHint("##Filter", "Filter textures...", m_filter,
                           IM_ARRAYSIZE(m_filter));
  ImGui::Separator();

  std::string searchStr = m_filter;
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
