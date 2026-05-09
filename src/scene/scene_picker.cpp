#include "scene_picker.hpp"

#include <backend/interfaces/rhi_definitions.hpp>
#include <core/timers.hpp>

#include "scene.hpp"

// BVH Implementation Headers
#include <bvh/v2/bbox.h>
#include <bvh/v2/default_builder.h>
#include <bvh/v2/ray.h>
#include <bvh/v2/stack.h>
#include <bvh/v2/tri.h>


namespace scene
{

// Type aliases for implementation convenience
using Scalar = InstanceAccelerator::Scalar;
using Vec3 = InstanceAccelerator::Vec3;
using BBox = bvh::v2::BBox<Scalar, 3>;
using Ray = bvh::v2::Ray<Scalar, 3>;

/**********************************************************/
bool InstanceAccelerator::build(const Scene& scene)
/**********************************************************/
{
  SCOPED_TIMER("Build BVH");

  if (scene.instances.empty())
  {
    return false;
  }

  m_scene = &scene;

  // ------------------------------------------------------------
  // Build BLAS (Bottom-Level BVH) for every Mesh
  // ------------------------------------------------------------
  m_meshBvhs.clear();
  m_meshBvhs.resize(scene.meshes.size());

  bvh::v2::DefaultBuilder<Node>::Config config;
  config.quality = bvh::v2::DefaultBuilder<Node>::Quality::High;

  for (size_t i = 0; i < scene.meshes.size(); ++i)
  {
    const auto& mesh = scene.meshes[i];

    // Validation check
    if (mesh.rawBufferIndex >= scene.meshData.size())
      continue;

    const uint8_t* bufferPtr = scene.meshData[mesh.rawBufferIndex].data();
    uint32_t triCount = mesh.triMesh.indices.count / 3;

    std::vector<BBox> triBboxes(triCount);
    std::vector<Vec3> triCenters(triCount);

    for (uint32_t t = 0; t < triCount; ++t)
    {
      glm::uvec3 idx = getTriangleIndices(mesh, bufferPtr, t);

      // Note: Since getAttribute is private and defined in this file,
      // the compiler will instantiate it correctly for glm::vec3.
      glm::vec3 v0 = getAttribute<glm::vec3>(mesh, bufferPtr, idx.x);
      glm::vec3 v1 = getAttribute<glm::vec3>(mesh, bufferPtr, idx.y);
      glm::vec3 v2 = getAttribute<glm::vec3>(mesh, bufferPtr, idx.z);

      auto min = glm::min(v0, glm::min(v1, v2));
      auto max = glm::max(v0, glm::max(v1, v2));

      triBboxes[t] = BBox(Vec3(min.x, min.y, min.z), Vec3(max.x, max.y, max.z));
      triCenters[t] = triBboxes[t].get_center();
    }

    m_meshBvhs[i] =
        bvh::v2::DefaultBuilder<Node>::build(triBboxes, triCenters, config);
  }

  // ------------------------------------------------------------
  // Build TLAS (Top-Level BVH) for Instances
  // ------------------------------------------------------------
  std::vector<BBox> instBboxes(scene.instances.size());
  std::vector<Vec3> instCenters(scene.instances.size());

  for (size_t i = 0; i < scene.instances.size(); ++i)
  {
    const auto& inst = scene.instances[i];
    const auto& mesh = scene.meshes[inst.meshIndex];

    glm::vec3 min = mesh.bbox.min;
    glm::vec3 max = mesh.bbox.max;

    glm::mat4 trans = inst.transform;
    glm::vec3 corners[8] = {{min.x, min.y, min.z}, {min.x, min.y, max.z},
                            {min.x, max.y, min.z}, {min.x, max.y, max.z},
                            {max.x, min.y, min.z}, {max.x, min.y, max.z},
                            {max.x, max.y, min.z}, {max.x, max.y, max.z}};

    glm::vec3 wMin(FLT_MAX), wMax(-FLT_MAX);
    for (auto& c : corners)
    {
      glm::vec3 wc = glm::vec3(trans * glm::vec4(c, 1.0f));
      wMin = glm::min(wMin, wc);
      wMax = glm::max(wMax, wc);
    }

    instBboxes[i] =
        BBox(Vec3(wMin.x, wMin.y, wMin.z), Vec3(wMax.x, wMax.y, wMax.z));
    instCenters[i] = instBboxes[i].get_center();
  }

  m_tlas =
      bvh::v2::DefaultBuilder<Node>::build(instBboxes, instCenters, config);
  return true;
}

// --------------------------------------------------------------------------
// Intersect
// --------------------------------------------------------------------------

/**********************************************************/
std::optional<RayHit> InstanceAccelerator::intersect(const glm::vec3& origin,
                                                     const glm::vec3& dir,
                                                     float tmin,
                                                     float tmax) const
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  if (!m_scene || m_tlas.nodes.empty())
  {
    return std::nullopt;
  }

  Ray ray(Vec3(origin.x, origin.y, origin.z), Vec3(dir.x, dir.y, dir.z), tmin,
          tmax);

  bool hitAny = false;
  RayHit bestHit{};
  bestHit.t = FLT_MAX;

  static const int StackSize = 64;
  bvh::v2::SmallStack<Bvh::Index, StackSize> tlasStack;
  bvh::v2::SmallStack<Bvh::Index, StackSize> blasStack;

  // Traverse TLAS (Instances)
  m_tlas.intersect<false, true>(
      ray, m_tlas.get_root().index, tlasStack,
      [&](size_t begin, size_t end)
      {
        for (size_t i = begin; i < end; ++i)
        {
          size_t instanceId = m_tlas.prim_ids[i];
          const auto& instance = m_scene->instances[instanceId];
          const auto& mesh = m_scene->meshes[instance.meshIndex];

          const auto& blas = m_meshBvhs[instance.meshIndex];
          if (blas.nodes.empty())
            continue;

          const uint8_t* bufferPtr =
              m_scene->meshData[mesh.rawBufferIndex].data();

          // Inverse transform ray to local space
          glm::mat4 invTransform = glm::inverse(instance.transform);
          glm::vec4 localOrg = invTransform * glm::vec4(ray.org[0], ray.org[1],
                                                        ray.org[2], 1.0f);
          glm::vec4 localDir = invTransform * glm::vec4(ray.dir[0], ray.dir[1],
                                                        ray.dir[2], 0.0f);

          Ray localRay(Vec3(localOrg.x, localOrg.y, localOrg.z),
                       Vec3(localDir.x, localDir.y, localDir.z), tmin, tmax);

          // Traverse BLAS (Triangles)
          blas.intersect<false, true>(
              localRay, blas.get_root().index, blasStack,
              [&](size_t bBegin, size_t bEnd)
              {
                bool blasHit = false;
                for (size_t k = bBegin; k < bEnd; ++k)
                {
                  uint32_t primID = blas.prim_ids[k];

                  glm::uvec3 indices =
                      getTriangleIndices(mesh, bufferPtr, primID);

                  glm::vec3 v0 =
                      getAttribute<glm::vec3>(mesh, bufferPtr, indices.x);
                  glm::vec3 v1 =
                      getAttribute<glm::vec3>(mesh, bufferPtr, indices.y);
                  glm::vec3 v2 =
                      getAttribute<glm::vec3>(mesh, bufferPtr, indices.z);

                  bvh::v2::PrecomputedTri<Scalar> tri(Vec3(v0.x, v0.y, v0.z),
                                                      Vec3(v1.x, v1.y, v1.z),
                                                      Vec3(v2.x, v2.y, v2.z));

                  if (auto hit = tri.intersect(localRay))
                  {
                    auto [localT, u, v] = *hit;

                    // Transform back to world for correct depth comparison
                    glm::vec3 localHitPos =
                        glm::vec3(localOrg) + glm::vec3(localDir) * localT;
                    glm::vec3 worldHitPos = glm::vec3(
                        instance.transform * glm::vec4(localHitPos, 1.0f));
                    float worldT = glm::distance(
                        glm::vec3(ray.org[0], ray.org[1], ray.org[2]),
                        worldHitPos);

                    if (worldT < ray.tmax)
                    {
                      ray.tmax = worldT;
                      localRay.tmax = localT;  // Optimize local traversal
                      hitAny = true;
                      blasHit = true;
                      bestHit = {(uint32_t) instanceId, primID, worldT, u, v};
                    }
                  }
                }
                return blasHit;
              });
        }
        return hitAny;
      });

  if (hitAny)
    return bestHit;
  return std::nullopt;
}

}  // namespace scene
