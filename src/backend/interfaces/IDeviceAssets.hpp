#pragma once
#include <cstdint>

#include "scene/gltf/gltf_utils.hpp"

class IDeviceAssets
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  using BufferAddr = uint8_t*;
  using BufferID = uint32_t;

  virtual void deinit() = 0;

  virtual void beginUploading() = 0;
  virtual void endUploading() = 0;

  // Resources
  virtual TextureID uploadTexture(const std::string& filepath) = 0;
  virtual void finalizeSceneResources(gltf::Scene& resources) = 0;

  // Add meshes
  virtual std::pair<BufferAddr, BufferID>
  uploadGltfBuffer(const tinygltf::Model& model) = 0;
  virtual std::pair<BufferAddr, BufferID>
  uploadPrimitiveMeshBuffer(const nvutils::PrimitiveMesh& primMesh,
                            uint32_t* verticesOffset = nullptr) {};

  virtual void addMeshes(size_t count, BufferID) = 0;
};
