#pragma once

#ifdef __APPLE__

#include <memory>

#include "backend/interfaces/IRenderContext.hpp"
#include "renderer/interfaces/IRenderGraph.hpp"

class MetalContextManager;
class MetalDeviceAssets;
struct MetalRasterPassData;

//------------------------------------------------------------
// MetalRasterPass
//------------------------------------------------------------
// Rasterises a scene using Metal.  Implements the platform-agnostic
// IRenderPass so the MetalRenderer can drive it through a RenderGraph.
//
// On init() the pass compiles an MSL vertex + fragment shader pair and
// creates a MTLRenderPipelineState.  On every execute() it iterates the
// scene instances, binds the per-mesh Metal buffers and issues indexed
// draw calls into the already-open render encoder supplied by
// MetalBackend.
class MetalRasterPass : public IRenderPass {
public:
  MetalRasterPass(MetalContextManager *ctx, MetalDeviceAssets *assets);
  ~MetalRasterPass() override;

  // IRenderPass – context-agnostic lifecycle (context stored at construction)
  void init() override;
  void deinit() override;
  void setup(PassBuilder &builder) override;
  void execute(const IRenderContext &ctx) override;

private:
  void createPipeline();

  MetalContextManager *m_ctx;
  MetalDeviceAssets   *m_assets;
  std::unique_ptr<MetalRasterPassData> m_data;
};

#endif // __APPLE__
