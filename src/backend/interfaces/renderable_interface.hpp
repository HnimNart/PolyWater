#pragma once

#include <volk/volk.h>

#include <memory>
#include <vector>

#include "backend/interfaces/render_context_interface.hpp"

class IRenderable
{
public:
  virtual ~IRenderable() = default;
  // Called during the command buffer recording phase
  virtual void onRender(const IRenderContext& ctx) = 0;
};

class RenderRegistry
{
public:
  void registerElement(std::shared_ptr<IRenderable> element)
  {
    m_elements.push_back(std::move(element));
  }

  const std::vector<std::shared_ptr<IRenderable>>& getElements() const
  {
    return m_elements;
  }

private:
  std::vector<std::shared_ptr<IRenderable>> m_elements;
};
