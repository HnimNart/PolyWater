#pragma once

#include <imgui.h>
#include <imgui_internal.h>

#include <functional>

#include <glm/glm.hpp>

#include "app/app_element_interface.hpp"
#include "core/math.hpp"
#include "scene/scene_picker.hpp"
#include "scene/scene_resources.hpp"

// Forward Declarations
namespace shaderio
{
struct Instance;
}
namespace core
{
class CameraManipulator;
}

namespace app
{

class GeometryPickerElement final : public IAppElement
{
public:
  using SelectionCallback = std::function<void(InstanceID)>;

  GeometryPickerElement(const scene::SceneResourcesManager& sceneResources,
                        std::shared_ptr<core::CameraManipulator> camera);

  void onSceneUpdate(const scene::SceneResourcesManager& sceneResources);

  void onAttach(Application* app) override;
  void onUIRender() override;

  // Set the function to call when an object is clicked
  void setSelectionCallback(SelectionCallback callback)
  {
    m_onSelect = std::move(callback);
  }

  std::optional<InstanceID> pickObject(glm::vec2 mousePos);

private:
  core::Ray getRayFromMouse(float mouseX, float mouseY, float width,
                            float height);
  InstanceID pickObject(float mouseX, float mouseY, float width, float height);
  void drawGeometryModifier();
  void drawRotationBall(const glm::vec3& center, float radius);

  // References
  const scene::SceneResourcesManager& m_sceneResources;
  std::shared_ptr<core::CameraManipulator> m_camera;
  Application* m_app = nullptr;
  scene::InstanceAccelerator m_accel;

  SelectionCallback m_onSelect{};
  InstanceID m_instanceSelected = -1;
  bool m_showModifier = true;
  enum class Axis
  {
    Undefined,
    X,
    Y,
    Z
  };
  Axis m_activeAxis = Axis::Undefined;
  ImVec2 m_lastMousePos;
};

}  // namespace app
