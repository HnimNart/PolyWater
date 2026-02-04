#pragma once
#include <vector>

#include "RHI_definitions.hpp"

struct ResourceUsage
{
  RenderOutput resource;
  ResourceState state;
  PipelineStage stage;

  // Helper: Is this a write operation?
  bool isWrite() const
  {
    return state == ResourceState::RenderTarget ||
           state == ResourceState::DepthWrite ||
           state == ResourceState::General ||
           state == ResourceState::TransferDst;
  }
};

class PassBuilder
{
public:
  // What resources do i need to read?
  void read(RenderOutput name, PipelineStage stage, ResourceState state)
  {
    m_usages.push_back({name, state, stage});
  }

  // "I need to write to this Render Target"
  void write(RenderOutput name, PipelineStage stage, ResourceState state)
  {
    m_usages.push_back({name, state, stage});
  }

  const std::vector<ResourceUsage>& getUsages() const { return m_usages; }

private:
  std::vector<ResourceUsage> m_usages;
};
