#include "render_graph_interface.hpp"

#include <iostream>

void RenderGraph::addPass(std::unique_ptr<IRenderPass> pass)
{
  m_passes.push_back(std::move(pass));
}

void RenderGraph::init()
{
  for (auto& p : m_passes)
  {
    p->init();
  }
}

void RenderGraph::deinit()
{
  for (auto& p : m_passes)
  {
    p->deinit();
  }
  m_passes.clear();
}

void RenderGraph::compile()
{
  m_barriers.clear();
  m_barriers.resize(m_passes.size());
  m_finalBarriers.clear();
  m_finalStates.clear();

  struct CurrentState
  {
    ResourceState state = ResourceState::Undefined;
    PipelineStage stage = PipelineStage::TopOfPipe;
    bool hasBeenProduced = false;  // Track if someone has written to this
  };

  std::unordered_map<RenderOutput, CurrentState> globalState;

  for (size_t i = 0; i < m_passes.size(); ++i)
  {
    PassBuilder builder;
    m_passes[i]->setup(builder);

    for (const auto& usage : builder.getUsages())
    {
      CurrentState& current = globalState[usage.resource];

      // --- VALIDATION CHECK ---
      // If we are reading but nobody has written to this resource yet...
      if (!usage.isWrite() && !current.hasBeenProduced)
      {
        std::cerr << "[RenderGraph Warning] Pass " << i
                  << " is reading from Resource " << (int) usage.resource
                  << " but it has not been written to yet! (Missing Producer)"
                  << std::endl;
      }

      // Logic to determine if a barrier is needed
      bool stateChange = (current.state != usage.state);
      bool hazard = usage.isWrite();

      if (stateChange || hazard)
      {
        BarrierInfo barrier;
        barrier.resource = usage.resource;
        barrier.oldState = current.state;
        barrier.srcStage = current.stage;
        barrier.newState = usage.state;
        barrier.dstStage = usage.stage;

        m_barriers[i].push_back(barrier);
      }

      // Update the tracker
      current.state = usage.state;
      current.stage = usage.stage;

      // Mark as produced if this usage is a write operation
      if (usage.isWrite())
      {
        current.hasBeenProduced = true;
      }
    }

    // Collect intended final states from this pass
    for (const auto& [resource, finalIntent] : builder.getFinalStates())
    {
      m_finalStates[resource] = finalIntent;
    }
  }

  // Now that we know the very last state of every resource, check if any
  // of them need to be transitioned to a specific final exported state.
  for (const auto& [resource, finalIntent] : m_finalStates)
  {
    auto it = globalState.find(resource);
    if (it != globalState.end())
    {
      const CurrentState& current = it->second;
      ResourceState finalState = finalIntent.first;
      PipelineStage finalStage = finalIntent.second;

      if (current.state != finalState)
      {
        BarrierInfo barrier;
        barrier.resource = resource;
        barrier.oldState = current.state;
        barrier.srcStage = current.stage;
        barrier.newState = finalState;
        barrier.dstStage = finalStage;

        m_finalBarriers.push_back(barrier);
      }
    }
  }
}

void RenderGraph::execute(IRenderContext& ctx) const
{
  for (size_t i = 0; i < m_passes.size(); ++i)
  {
    // Switch to the slot this pass records into (no-op if already active).
    ctx.activatePass(m_passes[i]->cmdSlot());

    // Submit Barriers (The Context handles the API translation)
    if (!m_barriers[i].empty())
    {
      ctx.submitBarriers(m_barriers[i]);
    }

    // Execute Pass
    m_passes[i]->execute(ctx);
  }

  // Submit Final Export Barriers into the last active command buffer.
  if (!m_finalBarriers.empty())
  {
    ctx.submitBarriers(m_finalBarriers);
  }

  // End the last active command buffer.
  ctx.activatePass(PassCmdSlot::Count);
}
