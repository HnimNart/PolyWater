#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "math.hpp"

// -----------------------------------------------------------------------
// core::toQuat / core::fromQuat – round-trip identity
// -----------------------------------------------------------------------

TEST(MathToQuat, IdentityRoundTrip)
{
  // Identity quaternion stored as (x,y,z,w)
  glm::vec4 raw(0.0f, 0.0f, 0.0f, 1.0f);
  glm::quat q = core::toQuat(raw);
  glm::vec4 back = core::fromQuat(q);

  EXPECT_NEAR(back.x, raw.x, 1e-5f);
  EXPECT_NEAR(back.y, raw.y, 1e-5f);
  EXPECT_NEAR(back.z, raw.z, 1e-5f);
  EXPECT_NEAR(back.w, raw.w, 1e-5f);
}

TEST(MathToQuat, ArbitraryQuatRoundTrip)
{
  // 90-degree rotation around Z axis: quat = (0, 0, sin45, cos45)
  const float s = 0.7071067812f;
  glm::vec4 raw(0.0f, 0.0f, s, s);
  glm::quat q = core::toQuat(raw);
  glm::vec4 back = core::fromQuat(q);

  EXPECT_NEAR(back.x, raw.x, 1e-5f);
  EXPECT_NEAR(back.y, raw.y, 1e-5f);
  EXPECT_NEAR(back.z, raw.z, 1e-5f);
  EXPECT_NEAR(back.w, raw.w, 1e-5f);
}

// -----------------------------------------------------------------------
// core::eulerToQuat – sanity check via expected rotation
// -----------------------------------------------------------------------

TEST(MathEulerToQuat, ZeroEulerIsIdentity)
{
  glm::vec4 q = core::eulerToQuat(glm::vec3(0.0f, 0.0f, 0.0f));
  // identity quat: (x,y,z,w) = (0,0,0,1)
  EXPECT_NEAR(q.x, 0.0f, 1e-5f);
  EXPECT_NEAR(q.y, 0.0f, 1e-5f);
  EXPECT_NEAR(q.z, 0.0f, 1e-5f);
  EXPECT_NEAR(q.w, 1.0f, 1e-5f);
}

TEST(MathEulerToQuat, NinetyDegreesAroundY)
{
  glm::vec4 q = core::eulerToQuat(glm::vec3(0.0f, 90.0f, 0.0f));
  // 90° around Y: quat = (0, sin45, 0, cos45)
  const float s = 0.7071067812f;
  EXPECT_NEAR(q.x, 0.0f, 1e-5f);
  EXPECT_NEAR(q.y, s, 1e-5f);
  EXPECT_NEAR(q.z, 0.0f, 1e-5f);
  EXPECT_NEAR(q.w, s, 1e-5f);
}

// -----------------------------------------------------------------------
// core::composeTransform – translation, rotation, scale
// -----------------------------------------------------------------------

TEST(MathComposeTransform, IdentityTransform)
{
  glm::vec4 rot(0.0f, 0.0f, 0.0f, 1.0f);  // identity quat (x,y,z,w)
  glm::mat4 m = core::composeTransform(glm::vec3(0.0f), rot, glm::vec3(1.0f));

  // Should be identity matrix
  glm::mat4 identity(1.0f);
  for (int c = 0; c < 4; ++c)
    for (int r = 0; r < 4; ++r)
      EXPECT_NEAR(m[c][r], identity[c][r], 1e-5f)
          << "  column=" << c << " row=" << r;
}

TEST(MathComposeTransform, PureTranslation)
{
  glm::vec3 t(3.0f, -1.0f, 7.0f);
  glm::vec4 rot(0.0f, 0.0f, 0.0f, 1.0f);
  glm::mat4 m = core::composeTransform(t, rot, glm::vec3(1.0f));

  glm::vec4 p = m * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  EXPECT_NEAR(p.x, t.x, 1e-5f);
  EXPECT_NEAR(p.y, t.y, 1e-5f);
  EXPECT_NEAR(p.z, t.z, 1e-5f);
}

TEST(MathComposeTransform, PureScale)
{
  glm::vec4 rot(0.0f, 0.0f, 0.0f, 1.0f);
  glm::vec3 scale(2.0f, 3.0f, 4.0f);
  glm::mat4 m = core::composeTransform(glm::vec3(0.0f), rot, scale);

  glm::vec4 p = m * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  EXPECT_NEAR(p.x, 2.0f, 1e-5f);
  EXPECT_NEAR(p.y, 3.0f, 1e-5f);
  EXPECT_NEAR(p.z, 4.0f, 1e-5f);
}

// -----------------------------------------------------------------------
// core::rayAABBIntersection
// -----------------------------------------------------------------------

TEST(RayAABBIntersection, HitsBox)
{
  core::Ray ray;
  ray.origin = glm::vec3(0.0f, 0.0f, -5.0f);
  ray.direction = glm::vec3(0.0f, 0.0f, 1.0f);

  glm::vec3 boxMin(-1.0f, -1.0f, 0.0f);
  glm::vec3 boxMax(1.0f, 1.0f, 2.0f);

  float t = 0.0f;
  bool hit = core::rayAABBIntersection(ray, boxMin, boxMax, t);

  EXPECT_TRUE(hit);
  EXPECT_NEAR(t, 5.0f, 1e-4f);
}

TEST(RayAABBIntersection, MissesBox)
{
  core::Ray ray;
  ray.origin = glm::vec3(5.0f, 0.0f, -5.0f);
  ray.direction = glm::vec3(0.0f, 0.0f, 1.0f);

  glm::vec3 boxMin(-1.0f, -1.0f, 0.0f);
  glm::vec3 boxMax(1.0f, 1.0f, 2.0f);

  float t = 0.0f;
  bool hit = core::rayAABBIntersection(ray, boxMin, boxMax, t);

  EXPECT_FALSE(hit);
}

TEST(RayAABBIntersection, RayOriginInsideBox)
{
  core::Ray ray;
  ray.origin = glm::vec3(0.0f, 0.0f, 0.0f);  // clearly inside [-1,1]^3
  ray.direction = glm::vec3(1.0f, 0.0f, 0.0f);

  glm::vec3 boxMin(-1.0f, -1.0f, -1.0f);
  glm::vec3 boxMax(1.0f, 1.0f, 1.0f);

  float t = 0.0f;
  bool hit = core::rayAABBIntersection(ray, boxMin, boxMax, t);

  // tmin is negative (entry behind origin), tmax positive → hit; t = tmin < 0
  EXPECT_TRUE(hit);
  EXPECT_LT(t, 0.0f);
}

TEST(RayAABBIntersection, RayBehindBox)
{
  core::Ray ray;
  ray.origin = glm::vec3(0.0f, 0.0f, 10.0f);
  ray.direction = glm::vec3(0.0f, 0.0f, 1.0f);  // pointing away

  glm::vec3 boxMin(-1.0f, -1.0f, 0.0f);
  glm::vec3 boxMax(1.0f, 1.0f, 2.0f);

  float t = 0.0f;
  bool hit = core::rayAABBIntersection(ray, boxMin, boxMax, t);

  EXPECT_FALSE(hit);
}
