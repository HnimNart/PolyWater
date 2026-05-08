#pragma once

#include <bvh/v2/bvh.h>
#include <bvh/v2/node.h>
#include <bvh/v2/vec.h>

#include <optional>
#include <vector>

#include <glm/glm.hpp>

namespace shaderio
{
struct MeshPrimitive;
}

namespace scene
{

struct Scene;

struct RayHit
{
  uint32_t instanceID;
  uint32_t primitiveIndex;  // Triangle index in the mesh
  float t;                  // Distance to hit
  float u, v;
};

class InstanceAccelerator
{
public:
  using Scalar = float;
  using Vec3 = bvh::v2::Vec<Scalar, 3>;
  using Node = bvh::v2::Node<Scalar, 3>;
  using Bvh = bvh::v2::Bvh<Node>;

  InstanceAccelerator() = default;

  /**
   * @brief Builds the TLAS (Instances) and BLAS (Meshes) for the scene.
   */
  [[nodiscard]] bool build(const Scene& scene);

  /**
   * @brief Raycasts against the scene structure.
   */
  std::optional<RayHit> intersect(const glm::vec3& origin, const glm::vec3& dir,
                                  float tmin = 1e-4f, float tmax = 1e20F) const;

private:
  Bvh m_tlas;                   // TLAS
  std::vector<Bvh> m_meshBvhs;  // BLAS(one per mesh)
  const Scene* m_scene = nullptr;
};

}  // namespace scene
