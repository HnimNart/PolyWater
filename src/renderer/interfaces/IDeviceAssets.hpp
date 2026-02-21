
#pragma once
#include <cstdint>
#include <span>

#include <core/shape/primitives.hpp>

#include "core/Image.hpp"
#include "scene/Scene.h"
#include "shaders/shared/structs.h"

namespace tinygltf {
class Model;
}

class IDeviceAssets {
public:
  using MeshID = uint32_t;
  using TextureID = uint32_t;
  using BufferAddr = uint8_t *;
  using BufferID = uint32_t;

  virtual ~IDeviceAssets() = default;

  // ---------------------------------------------------------------------------
  // Lifecycle & Batch Management
  // ---------------------------------------------------------------------------
  virtual void deinit() = 0;
  virtual void beginUploading() = 0;
  virtual void endUploading() = 0;

  // ---------------------------------------------------------------------------
  // Texture Management
  // ---------------------------------------------------------------------------
  virtual unsigned int reserveTextureSlot() = 0;
  virtual TextureID uploadTexture(const core::Image &image,
                                  TextureID id = -1) = 0;

  // ---------------------------------------------------------------------------
  // Geometry Upload (Buffers)
  // ---------------------------------------------------------------------------
  virtual std::pair<BufferAddr, BufferID>
  upload(const std::span<const unsigned char> &data) = 0;
  virtual std::pair<void *, BufferID> upload(const void *data,
                                             size_t bytes) = 0;

  // ---------------------------------------------------------------------------
  // Scene Registration
  // ---------------------------------------------------------------------------
  virtual void addMeshes(size_t count, BufferID) = 0;
  virtual void uploadSceneResoures(const Scene &resources) = 0;

  // ---------------------------------------------------------------------------
  // Update Scene buffers
  // ---------------------------------------------------------------------------
  virtual void update(const std::vector<shaderio::MeshPrimitive> &) = 0;
  virtual void update(const std::vector<shaderio::Instance> &) = 0;
  virtual void update(const std::vector<shaderio::Material> &) = 0;
};
