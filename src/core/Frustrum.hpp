#pragma once

#include <glm/glm.hpp>

struct Frustum {
  glm::vec4 planes[6];
};

/**********************************************************/
// Extracts the 6 clipping planes from a View-Projection matrix
Frustum extractFrustumPlanes(const glm::mat4 &vpMatrix)
/**********************************************************/
{
  Frustum f;
  // Left
  f.planes[0] = glm::vec4(
      vpMatrix[0][3] + vpMatrix[0][0], vpMatrix[1][3] + vpMatrix[1][0],
      vpMatrix[2][3] + vpMatrix[2][0], vpMatrix[3][3] + vpMatrix[3][0]);
  // Right
  f.planes[1] = glm::vec4(
      vpMatrix[0][3] - vpMatrix[0][0], vpMatrix[1][3] - vpMatrix[1][0],
      vpMatrix[2][3] - vpMatrix[2][0], vpMatrix[3][3] - vpMatrix[3][0]);
  // Bottom
  f.planes[2] = glm::vec4(
      vpMatrix[0][3] + vpMatrix[0][1], vpMatrix[1][3] + vpMatrix[1][1],
      vpMatrix[2][3] + vpMatrix[2][1], vpMatrix[3][3] + vpMatrix[3][1]);
  // Top
  f.planes[3] = glm::vec4(
      vpMatrix[0][3] - vpMatrix[0][1], vpMatrix[1][3] - vpMatrix[1][1],
      vpMatrix[2][3] - vpMatrix[2][1], vpMatrix[3][3] - vpMatrix[3][1]);
  // Near
  f.planes[4] = glm::vec4(
      vpMatrix[0][3] + vpMatrix[0][2], vpMatrix[1][3] + vpMatrix[1][2],
      vpMatrix[2][3] + vpMatrix[2][2], vpMatrix[3][3] + vpMatrix[3][2]);
  // Far
  f.planes[5] = glm::vec4(
      vpMatrix[0][3] - vpMatrix[0][2], vpMatrix[1][3] - vpMatrix[1][2],
      vpMatrix[2][3] - vpMatrix[2][2], vpMatrix[3][3] - vpMatrix[3][2]);

  // Normalize planes
  for (auto &plane : f.planes) {
    float length = glm::length(glm::vec3(plane));
    plane /= length;
  }
  return f;
}

/**********************************************************/
// Returns true if the AABB is inside or intersecting the frustum
bool isAABBInsideFrustum(const Frustum &frustum, const glm::vec3 &minBounds,
                         const glm::vec3 &maxBounds, const glm::mat4 &transform)
/**********************************************************/
{
  // Transform the 8 corners of the AABB to world space
  glm::vec3 corners[8] = {glm::vec3(minBounds.x, minBounds.y, minBounds.z),
                          glm::vec3(maxBounds.x, minBounds.y, minBounds.z),
                          glm::vec3(minBounds.x, maxBounds.y, minBounds.z),
                          glm::vec3(maxBounds.x, maxBounds.y, minBounds.z),
                          glm::vec3(minBounds.x, minBounds.y, maxBounds.z),
                          glm::vec3(maxBounds.x, minBounds.y, maxBounds.z),
                          glm::vec3(minBounds.x, maxBounds.y, maxBounds.z),
                          glm::vec3(maxBounds.x, maxBounds.y, maxBounds.z)};

  for (auto &corner : corners) {
    corner = glm::vec3(transform * glm::vec4(corner, 1.0f));
  }

  // Test against all 6 planes
  for (auto plane : frustum.planes) {
    int inCount = 8;
    for (auto corner : corners) {
      // If the point is outside the plane, decrement the count
      if (glm::dot(glm::vec3(plane), corner) + plane.w < 0.0f) {
        inCount--;
      }
    }
    // If all 8 corners are outside this specific plane, the object is
    // completely culled
    if (inCount == 0) {
      return false;
    }
  }
  return true;
}
