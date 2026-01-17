#pragma once
#include <cstdint>

#include "scene/gltf/gltf_utils.hpp"

class IDeviceResources
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  virtual void deinit() = 0;

  virtual void beginUploading() = 0;
  virtual void endUploading() = 0;

  // Resources
  virtual MeshID uploadGltfModel(const tinygltf::Model& model, gltf::Scene& resources) = 0;
  virtual TextureID uploadTexture(const std::string& filepath) = 0;
  virtual void finalizeSceneResources(gltf::Scene& resources) = 0;
};
