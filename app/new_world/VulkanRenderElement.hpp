#pragma once

#include <memory>

#include "app/IAppElement.hpp"
#include "app/elements/geometryPicker.hpp"
#include "core/Camera.hpp"
#include "scene/SceneManager.hpp"
#include <renderer/vulkan/Renderer.hpp>
#include <utility>

// Forward declarations to reduce compile time
namespace core {
class Application;
} // namespace core

class VulkanRenderer;

class VulkanRendererElement : public app::IAppElement {
public:
  VulkanRendererElement(std::string sceneFile)
      : m_sceneFile(std::move(sceneFile)) {}
  ~VulkanRendererElement() override = default;

  // -------------------------------------------------------------------------
  // 1. Lifecycle & Event Hooks
  // -------------------------------------------------------------------------
  void onAttach(app::Application *app) override;
  void onDetach() override;
  void onResize(WindowSize size) override;
  void onFileDrop(const std::filesystem::path &filename,
                  glm::vec2 mousePos) override;
  void onFileSelected(const std::filesystem::path &filename);

  // -------------------------------------------------------------------------
  // 2. Main Render Loop Steps
  // -------------------------------------------------------------------------
  void onPreRender() override;
  void onRender(const IRenderContext &ctx) override;
  void onEndFrame(const IRenderContext &frame) override;
  void onLastHeadlessFrame() override;

  // -------------------------------------------------------------------------
  // 3. User Interface & Editor
  // -------------------------------------------------------------------------
  void onUIMenu() override;
  void onUIRender() override;

  // Picked interaction
  void onGeometryPicked(InstanceID id);

  // -------------------------------------------------------------------------
  // 4. Accessors
  // -------------------------------------------------------------------------
  CameraPtr getCameraManipulator() const;
  const IRenderer *getRenderer() const;
  const SceneManager &getSceneManager() const;

  //

  void setGeometryPicker(std::shared_ptr<app::GeometryPickerElement> picker)
  {
    m_geometryPicker = std::move(picker);
  }

private:
  // -------------------------------------------------------------------------
  // Internal UI & Logic Helpers
  // -------------------------------------------------------------------------

  void init();
  void clear();
  void loadScene(const std::filesystem::path &filename);
  void processPendingResources();
  void processPendingTexture(SceneResourcesManager &resourceMgr);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  // Core components
  app::Application *m_app = nullptr;
  std::unique_ptr<VulkanRenderer> m_renderer = nullptr;
  SceneManager m_sceneManager{};

  std::shared_ptr<app::GeometryPickerElement> m_geometryPicker = nullptr;

  // Editor/Render state
  bool m_hasChanged = false;

  std::string m_sceneFile{};
  std::string m_modelFileToLoad{};
  std::string m_envFileToLoad{};

  struct PendingTexture {
    std::string filename;
    InstanceID id;
  };
  std::optional<PendingTexture> m_pendingTexture{};
};
