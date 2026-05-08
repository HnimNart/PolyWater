#pragma once

#include <memory>
#include <utility>

#include <renderer/vulkan/vulkan_renderer.hpp>

#include "app/app_element_interface.hpp"
#include "app/elements/geometry_picker.hpp"
#include "app/widgets/instance_editor.hpp"
#include "app/widgets/light_editor.hpp"
#include "app/widgets/material_editor.hpp"
#include "app/widgets/meshes_editor.hpp"
#include "app/widgets/render_editor.hpp"
#include "app/widgets/texture_editor.hpp"
#include "core/camera.hpp"
#include "scene/scene_manager.hpp"

class VulkanRendererElement : public app::IAppElement
{
public:
  VulkanRendererElement(std::string sceneFile) :
      m_sceneFile(std::move(sceneFile))
  {
  }
  ~VulkanRendererElement() override = default;

  // -------------------------------------------------------------------------
  // 1. Lifecycle & Event Hooks
  // -------------------------------------------------------------------------
  void onAttach(app::Application* app) override;
  void onDetach() override;
  void onResize(WindowSize size) override;
  void onFileDrop(const std::filesystem::path& filename,
                  glm::vec2 mousePos) override;
  void onFileSelected(const std::filesystem::path& filename);

  // -------------------------------------------------------------------------
  // 2. Main Render Loop Steps
  // -------------------------------------------------------------------------
  void onPreRender() override;
  void onRender(const IRenderContext& ctx) override;
  void onEndFrame(const IRenderContext& frame) override;
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
  IRenderer* getRenderer() const;
  const scene::SceneManager& getSceneManager() const;

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
  void loadScene(const std::filesystem::path& filename);
  void processPendingResources();
  void processPendingTexture(scene::SceneResourcesManager& resourceMgr);

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  // Core components
  app::Application* m_app = nullptr;
  std::unique_ptr<vkb::VulkanRenderer> m_renderer = nullptr;
  scene::SceneManager m_sceneManager{};

  std::shared_ptr<app::GeometryPickerElement> m_geometryPicker = nullptr;

  // Editor/Render state
  bool m_hasChanged = false;

  // Widget instances (hold per-widget UI state)
  app::RenderEditor m_renderEditor;
  app::LightEditor m_lightEditor;
  app::MaterialEditor m_materialEditor;
  app::InstanceEditor m_instanceEditor;
  app::MeshEditor m_meshEditor;
  app::TextureEditor m_textureEditor;

  std::string m_sceneFile{};
  std::string m_modelFileToLoad{};
  std::string m_envFileToLoad{};

  struct PendingTexture
  {
    std::string filename;
    InstanceID id;
  };
  std::optional<PendingTexture> m_pendingTexture{};
};
