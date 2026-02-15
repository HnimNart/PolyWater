#pragma once

#include <glm/glm.hpp>
#include <optional>
#include <vector>

// Include necessary BVH types for member variables
#include <bvh/v2/bvh.h>
#include <bvh/v2/node.h>
#include <bvh/v2/vec.h>

// Forward declarations to keep header clean
struct Scene;
namespace shaderio {
struct MeshPrimitive;
}

struct RayHit {
  uint32_t instanceID;
  uint32_t primitiveIndex; // Triangle index in the mesh
  float t;
  float u, v;
};

class InstanceAccelerator {
public:
  using Scalar = float;
  using Vec3 = bvh::v2::Vec<Scalar, 3>;
  using Node = bvh::v2::Node<Scalar, 3>;
  using Bvh = bvh::v2::Bvh<Node>;

  InstanceAccelerator() = default;

  /**
   * @brief Builds the TLAS (Instances) and BLAS (Meshes) for the scene.
   */
  bool build(const Scene &scene);

  /**
   * @brief Raycasts against the scene structure.
   */
  std::optional<RayHit> intersect(const glm::vec3 &origin,
                                  const glm::vec3 &dir) const;

private:
  Bvh m_tlas; // Top-Level Acceleration Structure
  std::vector<Bvh>
      m_meshBvhs; // Bottom-Level Acceleration Structures (one per mesh)
  const Scene *m_scene = nullptr;
};
