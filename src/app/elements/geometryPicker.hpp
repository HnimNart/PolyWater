#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_internal.h>

#include "app/IAppElement.hpp"
#include "core/Math.hpp"
#include "scene/SceneResources.hpp"

#include "scene/ScenePicker.hpp"

// Forward Declarations
namespace shaderio {
struct Instance;
}
namespace core {
class CameraManipulator;
}

namespace app {

class GeometryPickerElement : public IAppElement {
public:
  using SelectionCallback = std::function<void(InstanceID)>;

  GeometryPickerElement(const SceneResourcesManager &sceneResources,
                        std::shared_ptr<core::CameraManipulator> camera);

  void onAttach(Application *app) override;
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
  Application *m_app = nullptr;
  InstanceAccelerator m_accel;

  SelectionCallback m_onSelect{};
  InstanceID m_instanceSelected = -1;
  bool m_showModifier = true;
  enum class Axis { Undefined, X, Y, Z };
  Axis m_activeAxis = Axis::Undefined;
  ImVec2 m_lastMousePos;
};

} // namespace app
