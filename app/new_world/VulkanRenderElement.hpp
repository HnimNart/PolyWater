#pragma once

#include <memory>

#include "app/IAppElement.hpp"
#include "core/Camera.hpp"
#include "scene/SceneManager.hpp"
#include <renderer/vulkan/Renderer.hpp>

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
  void onFileDrop(const std::filesystem::path &filename) override;

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

private:
  // -------------------------------------------------------------------------
  // Internal UI & Logic Helpers
  // -------------------------------------------------------------------------

  void init();
  void reset();
  void loadScene(const std::filesystem::path &filename);
  void renderMaterialsUI();
  void renderInstancesUI();
  void renderMeshesUI();

  void updateMaterialList();
  std::string m_matNamesList;
  std::vector<MaterialID> m_matIDs;
  std::unordered_map<uint32_t, int> m_matIDToIndex; // <MaterialID, ComboIndex>

  // -------------------------------------------------------------------------
  // Members
  // -------------------------------------------------------------------------
  // Core components
  app::Application *m_app = nullptr;
  std::unique_ptr<VulkanRenderer> m_renderer = nullptr;
  SceneManager m_sceneManager{};

  // Editor/Render state
  RenderMode m_renderMode = RenderMode::RAYTRACE;
  bool m_hasChanged = false;

  std::string m_sceneFile{};
};
