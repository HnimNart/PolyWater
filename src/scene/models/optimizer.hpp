#pragma once

#include <cstdint>
#include <filesystem>
#include <glm/glm.hpp>
#include <string>
#include <tiny_gltf.h>
#include <vector>

#include "obj_utils.hpp"

// --- Optimizer Specific Structs ---
struct Vertex {
  glm::vec3 pos;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec4 tangent;
  glm::vec4 color;
};

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
                      const std::vector<obj::LoadedMesh> &loadedMeshes,
                      const std::filesystem::path &cachePath);
