#pragma once

#ifdef __APPLE__

#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "renderer/interfaces/IDeviceAssets.hpp"

class MetalContextManager;
struct MetalDeviceAssetsData;

//------------------------------------------------------------
// MetalDeviceAssets
//------------------------------------------------------------
// Concrete Metal implementation of IDeviceAssets.
// Uploads mesh geometry as Metal buffers and provides per-mesh
// index buffers for the rasterisation pass.  Vertex attributes
// (positions, normals, UVs) are read directly from the raw mesh
// data buffers via GPU virtual addresses using the same BufferView
// layout as the Vulkan renderer.
//
// Note: The "address" field returned by upload() is always nullptr;
// GPU virtual addresses are resolved later in uploadSceneResoures()
// via MTLBuffer.gpuAddress and stored in MeshPrimitive.buffer.
class MetalDeviceAssets final : public IDeviceAssets {
public:
  explicit MetalDeviceAssets(MetalContextManager *ctx);
  ~MetalDeviceAssets() override;

  // IDeviceAssets – lifecycle
  void deinit() override;
  void beginUploading() override {}
  void endUploading() override {}

  // IDeviceAssets – geometry
  BufferHandle upload(const std::span<const uint8_t> &data) override;
  void destroyBuffer(BufferID id) override;
  void linkMeshToBuffer(MeshID meshId, BufferID bufferId) override;

  // Called after all meshes have been uploaded.  Builds per-mesh
  // interleaved vertex + index buffers using the MeshPrimitive layout.
  void uploadSceneResoures(const Scene &resources) override;

  // Metal-specific: returns the raw (unmodified) Metal buffer for a
  // given buffer ID, or nullptr when not found.  Used internally by
  // uploadSceneResoures().
  void *getRawMetalBuffer(BufferID id) const;
  // Returns the number of indices for the given mesh.
  uint32_t getIndexCount(MeshID meshId) const;
  // Returns true when the index buffer contains 32-bit indices.
  bool is32BitIndex(MeshID meshId) const;

  // Metal-specific: returns direct MTLBuffer handles for the GPU-side
  // scene resource arrays.  Used by MetalRasterPass to bind them as
  // explicit shader parameters (bypassing BDA pointer chains).
  void *getSceneInfoMetalBuffer() const;
  void *getInstancesMetalBuffer() const;
  void *getMeshPrimitivesMetalBuffer() const;
  void *getMaterialsMetalBuffer() const;

  // IDeviceAssets – textures (minimal stub)
  unsigned int reserveTextureSlot() override { return 0; }
  bool addAndUploadTexture(const core::Image &image, TextureID &id,
                           bool immediate = false) override;
  uint64_t getTextureHandle(TextureID id) override { return 0; }
  bool destroyTexture(TextureID id) override { return true; }

  // IDeviceAssets – dynamic updates (no-ops for Metal)
  void update(const std::vector<shaderio::MeshPrimitive> &) override {}
  void update(const std::vector<shaderio::Instance> &) override {}
  void update(const std::vector<shaderio::Material> &) override {}

  // ---------------------------------------------------------------------------
  // Slang-shader GPU scene resources
  // ---------------------------------------------------------------------------
  // Returns the GPU virtual address of the SceneResources struct buffer.
  // The struct holds typed GPU pointers to Instance[], MeshPrimitive[], and
  // Material[] arrays, all of which are built in uploadSceneResoures().
  uint64_t getSceneResourcesGpuAddress() const;

  // Returns the GPU virtual address of the per-frame SceneInfo buffer.
  uint64_t getSceneInfoGpuAddress() const;

  // Copies 'info' into the shared SceneInfo Metal buffer so that the current
  // frame's view/projection matrix and lighting data are visible to the GPU.
  // Must be called once per frame before MetalRasterPass::execute().
  void updateSceneInfo(const shaderio::SceneInfo &info);

  // Makes every GPU-address-chained buffer resident on the given render
  // command encoder.  Must be called once per frame, before any draw calls,
  // so the GPU can safely dereference the pointer fields in PushConstant and
  // SceneResources (sceneInfo, sceneResources, instances, meshes, materials,
  // and every raw mesh-data buffer pointed to by MeshPrimitive.buffer).
  void useResources(void *renderCommandEncoderHandle) const;

private:
  MetalContextManager *m_ctx;
  std::unique_ptr<MetalDeviceAssetsData> m_data;
};

#endif // __APPLE__
