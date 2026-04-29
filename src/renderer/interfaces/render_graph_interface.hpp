#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include "backend/interfaces/render_context_interface.hpp"
#include "backend/interfaces/pass_builder.hpp"
#include "backend/interfaces/rhi_definitions.hpp"

class IRenderPass
{
public:
  virtual ~IRenderPass() = default;
  virtual void init() = 0;
  virtual void setup(PassBuilder& builder) = 0;
  virtual void execute(IRenderContext& ctx) = 0;
  virtual void deinit() = 0;
};

class RenderGraph
{
public:
  RenderGraph(std::string name) : m_name(std::move(name)){};
  const std::string& name() const noexcept { return m_name; }

  void addPass(std::unique_ptr<IRenderPass> pass);

  void init();

  void deinit();

  /**
   * @brief Finds the first pass of type T in the graph.
   * @return Pointer to the pass of type T, or nullptr if not found.
   */
  template <typename T> T* findPass()
  {
    for (const auto& pass : m_passes)
    {
      // Try to cast the base pointer (IRenderPass*) to the derived type (T*)
      T* castedPass = dynamic_cast<T*>(pass.get());
      if (castedPass)
      {
        return castedPass;
      }
    }
    return nullptr;
  }

  void compile();

  // -----------------------------------------------------------------------
  // EXECUTE: Delegates to the Context
  // -----------------------------------------------------------------------
  void execute(IRenderContext& ctx) const;

private:
  std::vector<std::unique_ptr<IRenderPass>> m_passes;
  std::vector<std::vector<BarrierInfo>>
      m_barriers;  // List of barriers per pass

  std::unordered_map<RenderOutput, std::pair<ResourceState, PipelineStage>>
      m_finalStates;
  std::vector<BarrierInfo> m_finalBarriers;

  std::string m_name = "";
};
