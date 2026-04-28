#include <gtest/gtest.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

#include "bounding_box.hpp"

// -----------------------------------------------------------------------
// Default construction
// -----------------------------------------------------------------------

TEST(BboxDefaultConstruct, IsEmpty)
{
  nvutils::Bbox b;
  EXPECT_TRUE(b.isEmpty());
}

// -----------------------------------------------------------------------
// insert(glm::vec3)
// -----------------------------------------------------------------------

TEST(BboxInsertPoint, SinglePoint)
{
  nvutils::Bbox b;
  b.insert(glm::vec3(1.0f, 2.0f, 3.0f));

  EXPECT_FALSE(b.isEmpty());
  EXPECT_TRUE(b.isPoint());

  EXPECT_NEAR(b.min().x, 1.0f, 1e-6f);
  EXPECT_NEAR(b.min().y, 2.0f, 1e-6f);
  EXPECT_NEAR(b.min().z, 3.0f, 1e-6f);

  EXPECT_NEAR(b.max().x, 1.0f, 1e-6f);
  EXPECT_NEAR(b.max().y, 2.0f, 1e-6f);
  EXPECT_NEAR(b.max().z, 3.0f, 1e-6f);
}

TEST(BboxInsertPoint, TwoPoints)
{
  nvutils::Bbox b;
  b.insert(glm::vec3(-1.0f, -2.0f, -3.0f));
  b.insert(glm::vec3(1.0f, 2.0f, 3.0f));

  EXPECT_NEAR(b.min().x, -1.0f, 1e-6f);
  EXPECT_NEAR(b.max().x, 1.0f, 1e-6f);
  EXPECT_TRUE(b.isVolume());
}

// -----------------------------------------------------------------------
// Corners constructor
// -----------------------------------------------------------------------

TEST(BboxCornersConstructor, MatchesInsertResult)
{
  std::vector<glm::vec3> pts = {
      {0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 4.0f}, {-1.0f, 5.0f, 2.0f}};

  nvutils::Bbox b(pts);

  EXPECT_NEAR(b.min().x, -1.0f, 1e-6f);
  EXPECT_NEAR(b.min().y, 0.0f, 1e-6f);
  EXPECT_NEAR(b.min().z, 0.0f, 1e-6f);

  EXPECT_NEAR(b.max().x, 2.0f, 1e-6f);
  EXPECT_NEAR(b.max().y, 5.0f, 1e-6f);
  EXPECT_NEAR(b.max().z, 4.0f, 1e-6f);
}

// -----------------------------------------------------------------------
// center
// -----------------------------------------------------------------------

TEST(BboxCenter, SymmetricBox)
{
  nvutils::Bbox b(glm::vec3(-2.0f, -4.0f, -6.0f),
                  glm::vec3(2.0f, 4.0f, 6.0f));

  glm::vec3 c = b.center();
  EXPECT_NEAR(c.x, 0.0f, 1e-6f);
  EXPECT_NEAR(c.y, 0.0f, 1e-6f);
  EXPECT_NEAR(c.z, 0.0f, 1e-6f);
}

TEST(BboxCenter, AsymmetricBox)
{
  nvutils::Bbox b(glm::vec3(0.0f, 0.0f, 0.0f),
                  glm::vec3(4.0f, 6.0f, 8.0f));

  glm::vec3 c = b.center();
  EXPECT_NEAR(c.x, 2.0f, 1e-6f);
  EXPECT_NEAR(c.y, 3.0f, 1e-6f);
  EXPECT_NEAR(c.z, 4.0f, 1e-6f);
}

// -----------------------------------------------------------------------
// radius
// -----------------------------------------------------------------------

TEST(BboxRadius, UnitCube)
{
  nvutils::Bbox b(glm::vec3(-0.5f), glm::vec3(0.5f));
  // diagonal = sqrt(3), half = sqrt(3)/2 ≈ 0.8660
  EXPECT_NEAR(b.radius(), std::sqrt(3.0f) * 0.5f, 1e-5f);
}

// -----------------------------------------------------------------------
// rank (isPoint / isLine / isPlane / isVolume)
// -----------------------------------------------------------------------

TEST(BboxRank, PointIsRank0)
{
  nvutils::Bbox b;
  b.insert(glm::vec3(1.0f, 2.0f, 3.0f));
  EXPECT_EQ(b.rank(), 0u);
  EXPECT_TRUE(b.isPoint());
}

TEST(BboxRank, LineIsRank1)
{
  nvutils::Bbox b;
  b.insert(glm::vec3(0.0f, 0.0f, 0.0f));
  b.insert(glm::vec3(1.0f, 0.0f, 0.0f));
  EXPECT_EQ(b.rank(), 1u);
  EXPECT_TRUE(b.isLine());
}

TEST(BboxRank, PlaneIsRank2)
{
  nvutils::Bbox b;
  b.insert(glm::vec3(0.0f, 0.0f, 0.0f));
  b.insert(glm::vec3(1.0f, 1.0f, 0.0f));
  EXPECT_EQ(b.rank(), 2u);
  EXPECT_TRUE(b.isPlane());
}

TEST(BboxRank, VolumeIsRank3)
{
  nvutils::Bbox b(glm::vec3(-1.0f), glm::vec3(1.0f));
  EXPECT_EQ(b.rank(), 3u);
  EXPECT_TRUE(b.isVolume());
}

// -----------------------------------------------------------------------
// insert(Bbox)
// -----------------------------------------------------------------------

TEST(BboxInsertBbox, MergesTwoBboxes)
{
  nvutils::Bbox a(glm::vec3(-1.0f), glm::vec3(0.0f));
  nvutils::Bbox b(glm::vec3(0.0f), glm::vec3(1.0f));
  a.insert(b);

  EXPECT_NEAR(a.min().x, -1.0f, 1e-6f);
  EXPECT_NEAR(a.max().x, 1.0f, 1e-6f);
}

// -----------------------------------------------------------------------
// transform – identity transform should not change the bbox
// -----------------------------------------------------------------------

TEST(BboxTransform, IdentityPreservesBbox)
{
  nvutils::Bbox b(glm::vec3(-2.0f, -3.0f, -1.0f),
                  glm::vec3(2.0f, 3.0f, 1.0f));

  nvutils::Bbox t = b.transform(glm::mat4(1.0f));

  EXPECT_NEAR(t.min().x, b.min().x, 1e-5f);
  EXPECT_NEAR(t.min().y, b.min().y, 1e-5f);
  EXPECT_NEAR(t.min().z, b.min().z, 1e-5f);

  EXPECT_NEAR(t.max().x, b.max().x, 1e-5f);
  EXPECT_NEAR(t.max().y, b.max().y, 1e-5f);
  EXPECT_NEAR(t.max().z, b.max().z, 1e-5f);
}

TEST(BboxTransform, TranslationShiftsBox)
{
  nvutils::Bbox b(glm::vec3(0.0f), glm::vec3(1.0f));
  glm::mat4 t = glm::translate(glm::mat4(1.0f), glm::vec3(5.0f, 0.0f, 0.0f));

  nvutils::Bbox bt = b.transform(t);

  EXPECT_NEAR(bt.min().x, 5.0f, 1e-5f);
  EXPECT_NEAR(bt.max().x, 6.0f, 1e-5f);
}

// -----------------------------------------------------------------------
// operator+= (expand bbox by a uniform margin)
// -----------------------------------------------------------------------

TEST(BboxExpand, UniformMargin)
{
  nvutils::Bbox b(glm::vec3(0.0f), glm::vec3(2.0f));
  b += 1.0f;

  EXPECT_NEAR(b.min().x, -1.0f, 1e-6f);
  EXPECT_NEAR(b.max().x, 3.0f, 1e-6f);
}
