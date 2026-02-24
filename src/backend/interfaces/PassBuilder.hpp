#pragma once
#include <unordered_map>
#include <vector>

#include "RHI_definitions.hpp"

struct ResourceUsage {
  RenderOutput resource;
  ResourceState state;
  PipelineStage stage;

  // Helper: Is this a write operation?
  bool isWrite() const {
    return state == ResourceState::RenderTarget ||
           state == ResourceState::DepthWrite ||
           state == ResourceState::General ||
           state == ResourceState::TransferDst;
  }
};

class PassBuilder {
public:
  // What resources do i need to read?
  void read(RenderOutput name, PipelineStage stage, ResourceState state) {
    m_usages.push_back({name, state, stage});
  }

  // "I need to write to this Render Target"
  void write(RenderOutput name, PipelineStage stage, ResourceState state) {
    m_usages.push_back({name, state, stage});
  }
  // Allow a pass to declare what state a resource must be in after the
  // pass
  void setFinalState(RenderOutput resource, ResourceState state,
                     PipelineStage stage = PipelineStage::BottomOfPipe) {
    m_finalStates[resource] = {state, stage};
  }
  const auto &getFinalStates() const { return m_finalStates; }

  const std::vector<ResourceUsage> &getUsages() const { return m_usages; }

private:
  std::vector<ResourceUsage> m_usages;
  std::unordered_map<RenderOutput, std::pair<ResourceState, PipelineStage>>
      m_finalStates;
};
