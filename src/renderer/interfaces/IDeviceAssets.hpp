
#pragma once
#include <cstdint>
#include <span>

#include <core/shape/primitives.hpp>

#include "core/Image.hpp"
#include "scene/Scene.h"
#include "shaders/shared/structs.h"

namespace tinygltf
{
class Model;
}

class IDeviceAssets
{
public:
  using MeshID = uint32_t;
  using TextureID = uint32_t;
  using BufferAddr = uint64_t;
  using BufferID = uint32_t;

  struct BufferHandle
  {
    BufferAddr address = 0;  // GPU pointer for shaders (BDA)
    BufferID id = 0;         // Index for the Asset Manager's vector

    // Optional: helper to check if valid
    /**********************************************************/
    bool isValid() const
    /**********************************************************/
    {
      return address != 0;
    }
    /**********************************************************/
    template <typename T> T* as() const
    /**********************************************************/
    {
      return reinterpret_cast<T*>(static_cast<uintptr_t>(address));
    }
    /**********************************************************/
    BufferAddr get() const
    /**********************************************************/
    {
      return address;
    }
  };

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
  virtual bool addAndUploadTexture(const core::Image& image, TextureID& id,
                                   bool immediate = false) = 0;
  virtual uint64_t getTextureHandle(TextureID id) = 0;
  virtual bool destroyTexture(TextureID id) = 0;

  // ---------------------------------------------------------------------------
  // Geometry Upload (Buffers)
  // ---------------------------------------------------------------------------
  virtual BufferHandle upload(const std::span<const uint8_t>& data) = 0;
  virtual void destroyBuffer(BufferID id) = 0;

  // ---------------------------------------------------------------------------
  // Scene Registration
  // ---------------------------------------------------------------------------
  virtual void linkMeshToBuffer(MeshID meshId, BufferID bufferId) = 0;
  virtual void uploadSceneResoures(const Scene& resources) = 0;

  // ---------------------------------------------------------------------------
  // Update Scene buffers
  // ---------------------------------------------------------------------------
  virtual void update(const std::vector<shaderio::MeshPrimitive>&) = 0;
  virtual void update(const std::vector<shaderio::Instance>&) = 0;
  virtual void update(const std::vector<shaderio::Material>&) = 0;
};
