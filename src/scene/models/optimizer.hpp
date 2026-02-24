#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "obj_utils.hpp"

struct OptimizedPayload {
  std::vector<uint8_t> rawBuffer;                  // Single GPU buffer
  std::vector<shaderio::MeshPrimitive> primitives; // Metadata for your engine
};

// Main Entry Point
OptimizedPayload processAndOptimizeGltf(const std::string &name,
                                        const tinygltf::Model &model,
                                        const std::filesystem::path &path);

OptimizedPayload
processAndOptimizeObj(const std::string &name,
                      const std::vector<obj::ObjMesh> &loadedMeshes,
                      const std::filesystem::path &cachePath);
