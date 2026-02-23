#include "Frustum.hpp"

namespace core {

Frustum extractFrustumPlanes(const glm::mat4 &vpMatrix) {
  Frustum f;

  // Left
  f.planes[0] = vpMatrix[3] + vpMatrix[0];
  // Right
  f.planes[1] = vpMatrix[3] - vpMatrix[0];
  // Bottom
  f.planes[2] = vpMatrix[3] + vpMatrix[1];
  // Top
  f.planes[3] = vpMatrix[3] - vpMatrix[1];
  // Near
  f.planes[4] = vpMatrix[3] + vpMatrix[2];
  // Far
  f.planes[5] = vpMatrix[3] - vpMatrix[2];

  // Normalize planes
  for (auto &plane : f.planes) {
    float length = glm::length(glm::vec3(plane));
    plane /= length;
  }
  return f;
}

bool isAABBInsideFrustum(const Frustum &frustum, const glm::vec3 &minBounds,
                         const glm::vec3 &maxBounds,
                         const glm::mat4 &transform) {
  // Transform the 8 corners of the AABB to world space
  glm::vec3 corners[8] = {{minBounds.x, minBounds.y, minBounds.z},
                          {maxBounds.x, minBounds.y, minBounds.z},
                          {minBounds.x, maxBounds.y, minBounds.z},
                          {maxBounds.x, maxBounds.y, minBounds.z},
                          {minBounds.x, minBounds.y, maxBounds.z},
                          {maxBounds.x, minBounds.y, maxBounds.z},
                          {minBounds.x, maxBounds.y, maxBounds.z},
                          {maxBounds.x, maxBounds.y, maxBounds.z}};

  for (auto &corner : corners) {
    corner = glm::vec3(transform * glm::vec4(corner, 1.0f));
  }

  // Test against all 6 planes
  for (const auto &plane : frustum.planes) {
    int outsideCount = 0;
    for (const auto &corner : corners) {
      if (glm::dot(glm::vec3(plane), corner) + plane.w < 0.0f) {
        outsideCount++;
      }
    }

    // If all 8 corners are outside this specific plane, it's culled
    if (outsideCount == 8) {
      return false;
    }
  }
  return true;
}

} // namespace core
