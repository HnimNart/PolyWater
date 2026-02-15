#include "geometryPicker.hpp"
#include "shaders/shared/structs.h" // For Instance definition

#include "core/Camera.hpp"
#include "core/timers.hpp"
#include <app/widgets/window.hpp>
#include <core/logger.hpp>
#include <glm/gtc/matrix_inverse.hpp> // For inverse transpose
#include <imgui.h>
#include <imgui_internal.h>

/**********************************************************/
app::GeometryPickerElement::GeometryPickerElement(
    const SceneResourcesManager &sceneResources,
    std::shared_ptr<core::CameraManipulator> camera)
    : m_sceneResources(sceneResources), m_camera(std::move(camera))
/**********************************************************/
{}

/**********************************************************/
void app::GeometryPickerElement::onAttach(Application *app)
/**********************************************************/
{
  m_app = app;
  LOGI("Adding Geometry Picker Element");
}

/**********************************************************/
void app::GeometryPickerElement::onUIRender()
/**********************************************************/
{
  ImGuiWindow *viewportWindow = ImGui::FindWindowByName("Viewport");
  if (!viewportWindow || !core::isWindowHovered(viewportWindow))
    return;

  // We only care about picking if Ctrl is held
  if (ImGui::GetIO().KeyCtrl) {
    bool clickedLeft = ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                       !ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    bool clickedRight = ImGui::IsMouseClicked(ImGuiMouseButton_Right) &&
                        !ImGui::IsMouseDragging(ImGuiMouseButton_Right);

    if (clickedLeft) {
      // 1. Calculate relative coordinates
      ImVec2 winPos = viewportWindow->Pos;
      ImVec2 winSize = viewportWindow->Size;
      ImVec2 mouseAbs = ImGui::GetMousePos();

      float mouseX = mouseAbs.x - winPos.x;
      float mouseY = mouseAbs.y - winPos.y;

      // 2. Raycast to find object
      int32_t hitIndex = pickObject(mouseX, mouseY, winSize.x, winSize.y);

      printf("Picked %d\n", hitIndex);

      if (m_onSelect) {
        m_onSelect(hitIndex);
      }
    } else if (clickedRight) {
      if (m_onSelect) {
        m_onSelect(-1);
      }
    }
  }
}

/**********************************************************/
math::Ray app::GeometryPickerElement::getRayFromMouse(float mouseX,
                                                      float mouseY, float width,
                                                      float height)
/**********************************************************/
{
  // Get Matrices from Camera
  const glm::mat4 &view = m_camera->getViewMatrix();

  // We need the projection matrix. Assuming a standard perspective setup
  // here. Note: If you store FOV in the camera manipulator, reconstruct it.
  glm::mat4 proj = m_camera->getPerspectiveMatrix();

  // 1. Normalized Device Coordinates (NDC) [-1, 1]
  float x = (2.0f * mouseX) / width - 1.0f;
  float y = (2.0f * mouseY) / height -
            1.0f; // Note: Vulkan/ImGui Y direction matters here

  // 2. Inverse Transform
  glm::vec4 rayClip = glm::vec4(x, y, 1.0f, 1.0f);
  glm::vec4 rayEye = glm::inverse(proj) * rayClip;
  rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);

  glm::vec3 rayWor = glm::vec3(glm::inverse(view) * rayEye);
  rayWor = glm::normalize(rayWor);

  return math::Ray{m_camera->getEye(), rayWor};
}

/**********************************************************/
InstanceID app::GeometryPickerElement::pickObject(float mouseX, float mouseY,
                                                  float width, float height)
/**********************************************************/
{

  SCOPED_TIMER_FUNC();

  const std::vector<shaderio::Instance> &instances =
      m_sceneResources.getInstances();
  if (instances.empty()) {
    return -1;
  }

  math::Ray ray = getRayFromMouse(mouseX, mouseY, width, height);

  InstanceID closestIdx = -1;
  float closestDist = FLT_MAX;

  // Loop through all instances in the scene
  for (size_t i = 0; i < instances.size(); ++i) {
    const auto &inst = instances[i];
    const auto &mesh = m_sceneResources.getMeshFromIdx(inst.meshIndex);

    // Optimization: Ray-AABB in Local Space.
    // Instead of transforming the AABB to world space
    // we transform the Ray to the Object's Local space.
    glm::mat4 invModel = glm::inverse(inst.transform);

    math::Ray localRay;
    localRay.origin = glm::vec3(invModel * glm::vec4(ray.origin, 1.0f));
    localRay.direction = glm::vec3(invModel * glm::vec4(ray.direction, 0.0f));

    glm::vec3 aabbMin = mesh.bbox.min;
    glm::vec3 aabbMax = mesh.bbox.max;

    float dist = std::numeric_limits<float>::max();
    if (math::rayAABBIntersection(localRay, aabbMin, aabbMax, dist)) {
      // dist is the distance in local space.
      if (dist < closestDist && dist > 0.0f) {
        closestDist = dist;
        closestIdx = static_cast<InstanceID>(i);
      }
    }
  }

  return closestIdx;
}

/**********************************************************/
void app::GeometryPickerElement::drawGeometryModifier()
/**********************************************************/
{
  if (m_instanceSelected == static_cast<uint>(-1)) {
    return;
  }

  auto &instances = m_sceneResources.getInstances();
  auto &inst = instances[m_instanceSelected];
  drawRotationBall(inst.translation, 1.0f);
}

/**********************************************************/
void app::GeometryPickerElement::drawRotationBall(const glm::vec3 &center,
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
  //     math::composeTransform(inst.translation, inst.rotation,
  // inst.scale);
  // m_sceneResources.onInstanceChange();
  // }
}
