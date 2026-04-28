#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Frustum.hpp"

// Helper: build an orthographic VP matrix so we know exactly what the frustum
// looks like.  Orthographic: [-hw,hw] x [-hh,hh] x [near,far]
static glm::mat4 makeOrthoVP(float hw, float hh, float near, float far)
{
  return glm::ortho(-hw, hw, -hh, hh, near, far);
}

// Helper: perspective VP (camera at origin looking down -Z)
static glm::mat4 makePerspectiveVP(float fovY, float aspect, float near,
                                   float far)
{
  return glm::perspective(glm::radians(fovY), aspect, near, far);
}

// -----------------------------------------------------------------------
// extractFrustumPlanes
// -----------------------------------------------------------------------

TEST(ExtractFrustumPlanes, OrthographicProducesValidPlanes)
{
  auto vp = makeOrthoVP(10.0f, 10.0f, 0.1f, 100.0f);
  core::Frustum f = core::extractFrustumPlanes(vp);

  // Each plane normal should be unit length after normalisation
  for (int i = 0; i < 6; ++i)
  {
    float len = glm::length(glm::vec3(f.planes[i]));
    EXPECT_NEAR(len, 1.0f, 1e-4f) << "  plane " << i;
  }
}

// -----------------------------------------------------------------------
// isAABBInsideFrustum  (orthographic frustum for deterministic tests)
// -----------------------------------------------------------------------

TEST(IsAABBInsideFrustum, BoxInsideFrustum)
{
  // ortho covers [-10,10] x [-10,10] x [0.1,100] depth range
  // GLM uses a right-handed convention where the camera looks down -Z,
  // so world-space z must be negative to be in front of the camera.
  auto vp = makeOrthoVP(10.0f, 10.0f, 0.1f, 100.0f);
  core::Frustum f = core::extractFrustumPlanes(vp);

  // Small box at z in [-2, -1], well inside the frustum
  glm::vec3 bMin(-1.0f, -1.0f, -2.0f);
  glm::vec3 bMax(1.0f, 1.0f, -1.0f);

  bool inside = core::isAABBInsideFrustum(f, bMin, bMax, glm::mat4(1.0f));
  EXPECT_TRUE(inside);
}

TEST(IsAABBInsideFrustum, BoxCompletelyOutside)
{
  auto vp = makeOrthoVP(10.0f, 10.0f, 0.1f, 100.0f);
  core::Frustum f = core::extractFrustumPlanes(vp);

  // Box far to the right, outside the x>10 boundary
  glm::vec3 bMin(50.0f, -1.0f, 1.0f);
  glm::vec3 bMax(60.0f, 1.0f, 2.0f);

  bool inside = core::isAABBInsideFrustum(f, bMin, bMax, glm::mat4(1.0f));
  EXPECT_FALSE(inside);
}

TEST(IsAABBInsideFrustum, BoxTranslatedInside)
{
  auto vp = makeOrthoVP(10.0f, 10.0f, 0.1f, 100.0f);
  core::Frustum f = core::extractFrustumPlanes(vp);

  // Box at origin in local space, translated in front of the camera (-Z direction)
  glm::vec3 bMin(-0.5f, -0.5f, -0.5f);
  glm::vec3 bMax(0.5f, 0.5f, 0.5f);

  // Translate 5 units down -Z (puts box at z in [-5.5, -4.5], inside the frustum)
  glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -5.0f));

  bool inside = core::isAABBInsideFrustum(f, bMin, bMax, t);
  EXPECT_TRUE(inside);
}

TEST(IsAABBInsideFrustum, BoxBehindCamera)
{
  // Perspective camera looking down -Z
  auto vp = makePerspectiveVP(60.0f, 1.0f, 0.1f, 100.0f);
  core::Frustum f = core::extractFrustumPlanes(vp);

  // Box behind the camera (z > 0 in view space means behind for GL convention)
  glm::vec3 bMin(-1.0f, -1.0f, 1.0f);
  glm::vec3 bMax(1.0f, 1.0f, 5.0f);

  bool inside = core::isAABBInsideFrustum(f, bMin, bMax, glm::mat4(1.0f));
  EXPECT_FALSE(inside);
}
