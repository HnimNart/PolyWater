#include "geometry_picker.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <app/widgets/window.hpp>
#include <core/logger.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include "core/camera.hpp"
#include "shaders/shared/structs.h"

/**********************************************************/
app::GeometryPickerElement::GeometryPickerElement(
    const SceneResourcesManager& sceneResources,
    std::shared_ptr<core::CameraManipulator> camera) :
    m_sceneResources(sceneResources), m_camera(std::move(camera))
/**********************************************************/
{
}

/**********************************************************/
void app::GeometryPickerElement::onSceneUpdate(
    const SceneResourcesManager& scene)
/**********************************************************/
{
  if (!m_accel.build(scene.data()))
  {
    throw std::runtime_error("[GeometryPickerElement] Failed to build "
                             "acceleration structure on scene update.");
  }
}

/**********************************************************/
void app::GeometryPickerElement::onAttach(Application* app)
/**********************************************************/
{
  m_app = app;
  LOGI("Adding Geometry Picker Element\n");
}

/**********************************************************/
void app::GeometryPickerElement::onUIRender()
/**********************************************************/
{
  if (!ImGui::GetIO().KeyCtrl)
    return;

  // Check for clicks first to avoid unnecessary window lookups
  bool left = ImGui::IsMouseClicked(0) && !ImGui::IsMouseDragging(0);
  bool right = ImGui::IsMouseClicked(1) && !ImGui::IsMouseDragging(1);

  if (left || right)
  {
    auto hitIndex =
        pickObject(glm::vec2(ImGui::GetMousePos().x, ImGui::GetMousePos().y));

    if (m_onSelect)
    {
      // If right click, or left click missed (-1), pass -1
      m_onSelect(right ? -1 : hitIndex.value_or(-1));
    }

    if (left && hitIndex.has_value())
    {
      LOGI("Picked %d\n", hitIndex.value());
    }
  }
}

/**********************************************************/
InstanceID app::GeometryPickerElement::pickObject(float mouseX, float mouseY,
                                                  float width, float height)
/**********************************************************/
{
  core::Ray ray = getRayFromMouse(mouseX, mouseY, width, height);
  auto hit = m_accel.intersect(ray.origin, ray.direction);
  return hit ? hit->instanceID : -1;
}

/**********************************************************/
std::optional<InstanceID>
app::GeometryPickerElement::pickObject(glm::vec2 mouseAbs)
/**********************************************************/
{
  ImGuiWindow* viewport = ImGui::FindWindowByName("Viewport");
  if (!viewport || !app::isWindowHovered(viewport))
    return std::nullopt;  // Using nullopt instead of -1 for optional

  float relX = mouseAbs.x - viewport->Pos.x;
  float relY = mouseAbs.y - viewport->Pos.y;

  return pickObject(relX, relY, viewport->Size.x, viewport->Size.y);
}

/**********************************************************/
// 4. Ray Generation (Math fix included)
/**********************************************************/
core::Ray app::GeometryPickerElement::getRayFromMouse(float mouseX,
                                                      float mouseY, float width,
                                                      float height)
/**********************************************************/
{
  // 1. NDC [-1, 1].
  float x = (2.0f * mouseX) / width - 1.0f;
  float y = (2.0f * mouseY) / height - 1.0f;

  glm::mat4 invProjView = glm::inverse(m_camera->getPerspectiveMatrix() *
                                       m_camera->getViewMatrix());

  // Near plane point
  glm::vec4 rayStartNDC(x, y, -1.0f, 1.0f);
  // Far plane point (or just a point in front)
  glm::vec4 rayEndNDC(x, y, 0.0f, 1.0f);

  glm::vec4 worldStart = invProjView * rayStartNDC;
  worldStart /= worldStart.w;
  glm::vec4 worldEnd = invProjView * rayEndNDC;
  worldEnd /= worldEnd.w;

  glm::vec3 dir = glm::normalize(glm::vec3(worldEnd - worldStart));

  return core::Ray{m_camera->getEye(), dir};
}

/**********************************************************/
void app::GeometryPickerElement::drawGeometryModifier()
/**********************************************************/
{
  if (m_instanceSelected == static_cast<uint>(-1))
  {
    return;
  }

  auto& instances = m_sceneResources.getInstances();
  auto& inst = instances[m_instanceSelected];
  drawRotationBall(inst.translation, 1.0f);
}

/**********************************************************/
void app::GeometryPickerElement::drawRotationBall(const glm::vec3& center,
                                                  float radius)
/**********************************************************/
{
  // ImDrawList *drawList = ImGui::GetForegroundDrawList();
  // ImGuiIO &io = ImGui::GetIO();

  // // 1. Viewport Setup (as you had it)
  // ImGuiWindow *window = ImGui::FindWindowByName("Viewport");
  // if (!window)
  //   return;
  // ImVec2 vMin = window->ContentRegionRect.Min;
  // ImVec2 vMax = window->ContentRegionRect.Max;
  // ImVec2 vSize = ImVec2(vMax.x - vMin.x, vMax.y - vMin.y);

  // auto project = [&](glm::vec3 p) -> ImVec2 {
  //   glm::mat4 view = m_camera->getViewMatrix();
  //   glm::mat4 proj = m_camera->getPerspectiveMatrix();
  //   proj[1][1] *= -1;
  //   glm::vec4 clip = (proj * view) * glm::vec4(p, 1.0f);
  //   if (clip.w <= 0)
  //     return ImVec2(-1, -1);
  //   glm::vec3 ndc = glm::vec3(clip) / clip.w;
  //   return ImVec2((ndc.x * 0.5f + 0.5f) * vSize.x + vMin.x,
  //                 (1.0f - (ndc.y * 0.5f + 0.5f)) * vSize.y + vMin.y);
  // };

  // // 2. Interaction Logic
  // auto &inst = m_sceneResources.getInstances()[m_instanceSelected];
  // bool isMouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);

  // if (!isMouseDown)
  //   m_activeAxis = Axis::Undefined;

  // auto handleCircle = [&](glm::vec3 axisA, glm::vec3 axisB, uint32_t color,
  //                         Axis axisType) {
  //   bool isHovered = false;
  //   ImVec2 prevPoint = ImVec2(-1, -1);

  //   // First pass: Draw and detect hover
  //   for (int i = 0; i <= 64; ++i) {
  //     float angle = (float)i / 64.0f * M_PI * 2.0f;
  //     glm::vec3 p =
  //         center + (axisA * cosf(angle) + axisB * sinf(angle)) * radius;
  //     ImVec2 screenPos = project(p);

  //     if (prevPoint.x != -1 && screenPos.x != -1) {
  //       // Check if mouse is near this line segment
  //       if (m_activeAxis == Axis::Undefined &&
  //           ImGui::IsMouseHoveringRect(
  //               ImVec2(fminf(prevPoint.x, screenPos.x) - 100,
  //                      fminf(prevPoint.y, screenPos.y) - 100),
  //               ImVec2(fmaxf(prevPoint.x, screenPos.x) + 100,
  //                      fmaxf(prevPoint.y, screenPos.y) + 100))) {
  //         isHovered = true;
  //         printf("Ys\n");
  //       }

  //       // Dynamic color: highlight if hovered or active
  //       uint32_t finalColor = (m_activeAxis == axisType ||
  //                              (m_activeAxis == Axis::Undefined &&
  //                              isHovered))
  //                                 ? ImGui::GetColorU32(ImVec4(1, 1, 0, 1))
  //                                 : color;

  //       drawList->AddLine(prevPoint, screenPos, finalColor,
  //                         (isHovered || m_activeAxis == axisType) ? 4.0f
  //                                                                 : 2.0f);
  //     }
  //     prevPoint = screenPos;
  //   }

  //   if (isHovered && ImGui::IsMouseClicked(0))
  //     m_activeAxis = axisType;
  // };

  // // 3. Draw & Axis Detection
  // handleCircle(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
  //              ImGui::GetColorU32(ImVec4(1, 0, 0, 1)), Axis::X);
  // handleCircle(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1),
  //              ImGui::GetColorU32(ImVec4(0, 1, 0, 1)), Axis::Y);
  // handleCircle(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0),
  //              ImGui::GetColorU32(ImVec4(0, 0, 1, 1)), Axis::Z);

  // // 4. Rotation Application
  // if (m_activeAxis != Axis::Undefined && ImGui::IsMouseDragging(0)) {
  //   ImVec2 delta = io.MouseDelta;
  //   float rotationSpeed = 0.01f;
  //   float angle =
  //       (abs(delta.x) > abs(delta.y) ? delta.x : -delta.y) * rotationSpeed;

  //   glm::vec3 rotationAxis(0);
  //   if (m_activeAxis == Axis::X)
  //     rotationAxis = glm::vec3(1, 0, 0);
  //   if (m_activeAxis == Axis::Y)
  //     rotationAxis = glm::vec3(0, 1, 0);
  //   if (m_activeAxis == Axis::Z)
  //     rotationAxis = glm::vec3(0, 0, 1);

  //   // Update orientation
  //   glm::quat currentQ(inst.rotation.w, inst.rotation.x, inst.rotation.y,
  //                      inst.rotation.z);
  //   glm::quat deltaQ = glm::angleAxis(angle, rotationAxis);
  //   glm::quat newQ = deltaQ * currentQ;

  // inst.rotation = glm::vec4(newQ.x, newQ.y, newQ.z, newQ.w);
  // inst.transform =
  //     core::composeTransform(inst.translation, inst.rotation,
  // inst.scale);
  // m_sceneResources.onInstanceChange();
  // }
}
