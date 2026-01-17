#pragma once
#include <cstdint>

#include "scene/gltf/gltf_utils.hpp"

class IDeviceResources
{
public:
  using MeshID = int;
  using TextureID = uint32_t;

  virtual void deinit() = 0;

  virtual void begin_uploading() = 0;
  virtual void end_uploading() = 0;

  // Resources
  virtual MeshID upload_gltf_model(const tinygltf::Model& model, gltf::Scene& resources) = 0;
  virtual TextureID upload_texture(const std::string& filepath) = 0;
  virtual void finalizeSceneResources(gltf::Scene& resources) = 0;
};
