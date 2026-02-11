#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "core/Math.hpp"
#include "app/IAppElement.hpp"
#include "scene/SceneResources.hpp"

// Forward Declarations
namespace shaderio {
struct Instance;
}
namespace core {
class CameraManipulator;
}

namespace core {

class GeometryPickerElement : public core::IAppElement {
public:
  using SelectionCallback = std::function<void(InstanceID)>;

  GeometryPickerElement(const SceneResourcesManager &sceneResources,
                        std::shared_ptr<core::CameraManipulator> camera);

  void onAttach(core::Application *app) override;
  void onUIRender() override;

  // Set the function to call when an object is clicked
  void setSelectionCallback(SelectionCallback callback) {
    m_onSelect = std::move(callback);
  }

private:
  math::Ray getRayFromMouse(float mouseX, float mouseY, float width,
                            float height);
  InstanceID pickObject(float mouseX, float mouseY, float width, float height);
  void drawGeometryModifier();
  void drawRotationBall(const glm::vec3 &center, float radius);

  // References
  const SceneResourcesManager &m_sceneResources;
  std::shared_ptr<core::CameraManipulator> m_camera;
  core::Application *m_app = nullptr;

  SelectionCallback m_onSelect;
  InstanceID m_instanceSelected = -1;
  bool m_showModifier = true;
  enum class Axis { Undefined, X, Y, Z };
  Axis m_activeAxis = Axis::Undefined;
  ImVec2 m_lastMousePos;
};

} // namespace core
