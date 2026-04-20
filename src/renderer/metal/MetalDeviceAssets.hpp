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
// interleaved vertex / index buffers for the rasterisation pass.
//
// Note: This implementation does NOT use Buffer Device Addresses (BDA)
// – a Vulkan concept.  The "address" field returned by upload() is
// always nullptr; callers must use getMetalBufferForMesh() to obtain
// the underlying id<MTLBuffer>.
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

  // Metal-specific: returns the interleaved vertex buffer for a mesh.
  void *getVertexMetalBuffer(MeshID meshId) const;
  // Metal-specific: returns the index buffer for a mesh.
  void *getIndexMetalBuffer(MeshID meshId) const;
  // Returns the number of indices for the given mesh.
  uint32_t getIndexCount(MeshID meshId) const;
  // Returns true when the index buffer contains 32-bit indices.
  bool is32BitIndex(MeshID meshId) const;

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

private:
  MetalContextManager *m_ctx;
  std::unique_ptr<MetalDeviceAssetsData> m_data;
};

#endif // __APPLE__
