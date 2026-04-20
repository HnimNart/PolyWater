#pragma once

#ifdef __APPLE__

#include <memory>

#include "app/IAppElement.hpp"
#include "core/Camera.hpp"
#include "scene/SceneManager.hpp"

class MetalRenderer;
class MetalBackend;

//------------------------------------------------------------
// MetalRendererElement
//------------------------------------------------------------
// An app::IAppElement that wraps the MetalRenderer and SceneManager
// so they participate in the Application lifecycle.
//
// Responsibilities:
//  - onAttach: create MetalRenderer, load default scene
//  - onPreRender: tick camera / scene updates
//  - onRender: invoke MetalRenderer::render(ctx)
//  - onUIRender: show Settings panel (camera, scene info)
//  - onFileDrop / onFileSelected: load a new scene or model
class MetalRendererElement : public app::IAppElement {
public:
  explicit MetalRendererElement(std::string sceneFile = "");
  ~MetalRendererElement() override = default;

  // -------------------------------------------------------------------------
  // IAppElement
  // -------------------------------------------------------------------------
  void onAttach(app::Application *app) override;
  void onDetach() override;
  void onResize(WindowSize size) override;
  void onFileDrop(const std::filesystem::path &filename,
                  glm::vec2 mousePos) override;
  void onFileSelected(const std::filesystem::path &filename);

  void onPreRender() override;
  void onRender(const IRenderContext &ctx) override;
  void onUIRender() override;
  void onUIMenu() override;

  // -------------------------------------------------------------------------
  // Accessors (used by wiring code in metalElement)
  // -------------------------------------------------------------------------
  CameraPtr getCameraManipulator() const;
  const SceneManager &getSceneManager() const;

private:
  void loadScene(const std::filesystem::path &filepath);
  void clear();

  app::Application *m_app = nullptr;
  std::unique_ptr<MetalRenderer> m_renderer;
  SceneManager m_sceneManager;

  std::string m_sceneFile;
  std::string m_modelFileToLoad;
};

#endif // __APPLE__
