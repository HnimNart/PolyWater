
#pragma once
#include <cstdint>

#include <nvutils/primitives.hpp>

#include "scene/Scene.h"
#include "shaders/shaderio.h"

namespace tinygltf
{
class Model;
}

class IDeviceAssets
{
public:
  using MeshID = uint32_t;
  using TextureID = uint32_t;
  using BufferAddr = uint8_t*;
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
  virtual TextureID uploadTexture(const std::string& filepath, TextureID) = 0;

  // ---------------------------------------------------------------------------
  // Geometry Upload (Buffers)
  // ---------------------------------------------------------------------------
  virtual std::pair<BufferAddr, BufferID>
  uploadGltfBuffer(const tinygltf::Model& model) = 0;

  /// TODO i think this is no needed
  virtual std::pair<BufferAddr, BufferID>
  uploadPrimitiveMeshBuffer(const nvutils::PrimitiveMesh& primMesh,
                            uint32_t* verticesOffset = nullptr) {};

  // ---------------------------------------------------------------------------
  // Scene Registration
  // ---------------------------------------------------------------------------
  virtual void addMeshes(size_t count, BufferID) = 0;
  virtual void finalizeSceneResources(Scene& resources) = 0;
};
