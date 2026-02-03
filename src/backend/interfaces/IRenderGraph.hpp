#pragma once

#include <memory>
#include <vector>

#include "IRenderContext.hpp"
#include "scene/SceneResources.hpp"

class IRenderPass
{
public:
  virtual ~IRenderPass() = default;
  virtual void init(class VulkanContextManager* core,
                    const SceneResourcesManager& scene) = 0;
  virtual void execute(const IRenderContext& ctx) = 0;
  virtual void deinit(class VulkanContextManager* core) = 0;
};

// 3. The Manager
class RenderGraph
{
public:
  void addPass(std::unique_ptr<IRenderPass> pass)
  {
    m_passes.push_back(std::move(pass));
  }

  void init(class VulkanContextManager* core,
            const SceneResourcesManager& scene)
  {
    for (auto& p : m_passes)
    {
      p->init(core, scene);
    }
  }

  void execute(const IRenderContext& ctx) const
  {
    for (auto& p : m_passes)
    {
      p->execute(ctx);
    }
  }

  void deinit(class VulkanContextManager* core)
  {
    for (auto& p : m_passes)
    {
      p->deinit(core);
    }
    m_passes.clear();
  }

private:
  std::vector<std::unique_ptr<IRenderPass>> m_passes;
};
