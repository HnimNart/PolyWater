#pragma once

#include <glm/glm.hpp>

namespace core
{

struct Frustum
{
  glm::vec4 planes[6];
};

/**
 * Extracts the 6 clipping planes from a View-Projection matrix.
 */
Frustum extractFrustumPlanes(const glm::mat4& vpMatrix);

/**
 * Returns true if the AABB is inside or intersecting the frustum.
 * Bounds are in local space; transform converts them to world space.
 */
bool isAABBInsideFrustum(const Frustum& frustum, const glm::vec3& minBounds,
                         const glm::vec3& maxBounds,
                         const glm::mat4& transform);

}  // namespace core
