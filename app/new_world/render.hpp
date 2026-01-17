#pragma once

#include <memory>

#include "core/Camera.hpp"  // Needed for CameraPtr
#include "core/application/IAppElement.hpp"
#include "scene/SceneManager.hpp"  // Needed for member m_scene_manager

// Forward declarations to reduce compile time
namespace core
{
class Application;
class VulkanBackend;
}  // namespace core
class VulkanSceneRenderer;

class RtBasic : public core::IAppElement
{
public:
  RtBasic() = default;
  ~RtBasic() override = default;

  // Interface Implementation
  void onAttach(core::Application* app) override;
  void onDetach() override;
  void onResize(WindowSize size) override;
  void onUIRender() override;
  void onPreRender() override;
  void onUIMenu() override;
  void onRender(FrameContext* ctx) override;
  void onEndFrame(const FrameContext* frame) override;

  // Accessor
  CameraPtr& getCameraManipulator();

private:
  // Custom methods
  void setup_scene();

private:
  // Application and core components
  core::Application* m_app = nullptr;
  // TODO who should own this?
  CameraPtr m_cameraManip{std::make_shared<nvutils::CameraManipulator>()};

  std::shared_ptr < VulkanSceneRenderer> m_renderer = nullptr;
  SceneManager m_scene_manager;

  // Ray tracing toggle
  bool m_useRayTracing = true;
};
