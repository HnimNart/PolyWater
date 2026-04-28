#include <gtest/gtest.h>

#include <string>

#include "Bimap.hpp"

// -----------------------------------------------------------------------
// insert & basic lookup
// -----------------------------------------------------------------------

TEST(Bimap, InsertAndLookup)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");

  auto r = bm.getRight(1);
  ASSERT_TRUE(r.has_value());
  EXPECT_EQ(*r, "one");

  auto l = bm.getLeft("one");
  ASSERT_TRUE(l.has_value());
  EXPECT_EQ(*l, 1);
}

TEST(Bimap, InsertMultipleEntries)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(2, "two");
  bm.insert(3, "three");

  EXPECT_EQ(bm.size(), 3u);

  EXPECT_EQ(*bm.getRight(2), "two");
  EXPECT_EQ(*bm.getLeft("three"), 3);
}

// -----------------------------------------------------------------------
// Missing keys return nullopt
// -----------------------------------------------------------------------

TEST(Bimap, MissingKeyReturnsNullopt)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");

  EXPECT_FALSE(bm.getRight(99).has_value());
  EXPECT_FALSE(bm.getLeft("missing").has_value());
}

// -----------------------------------------------------------------------
// 1:1 invariant – reinserting an existing left key replaces mapping
// -----------------------------------------------------------------------

TEST(Bimap, ReinsertingLeftKeyUpdatesMapping)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(1, "uno");  // overwrite

  EXPECT_EQ(*bm.getRight(1), "uno");
  // Old right value "one" must no longer be present
  EXPECT_FALSE(bm.getLeft("one").has_value());
  EXPECT_EQ(*bm.getLeft("uno"), 1);
  EXPECT_EQ(bm.size(), 1u);
}

TEST(Bimap, ReinsertingRightKeyUpdatesMapping)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(2, "one");  // "one" now maps to 2

  EXPECT_EQ(*bm.getRight(2), "one");
  // Old left key 1 must no longer map to "one"
  EXPECT_FALSE(bm.getRight(1).has_value());
  EXPECT_EQ(*bm.getLeft("one"), 2);
  EXPECT_EQ(bm.size(), 1u);
}

// -----------------------------------------------------------------------
// removeByLeft
// -----------------------------------------------------------------------

TEST(Bimap, RemoveByLeft)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(2, "two");

  bm.removeByLeft(1);

  EXPECT_FALSE(bm.getRight(1).has_value());
  EXPECT_FALSE(bm.getLeft("one").has_value());
  EXPECT_EQ(bm.size(), 1u);

  // Remaining entry untouched
  EXPECT_EQ(*bm.getRight(2), "two");
}

TEST(Bimap, RemoveByLeftNonExistent)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");

  // Removing a key that doesn't exist should be a no-op
  bm.removeByLeft(99);
  EXPECT_EQ(bm.size(), 1u);
}

// -----------------------------------------------------------------------
// removeByRight
// -----------------------------------------------------------------------

TEST(Bimap, RemoveByRight)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(2, "two");

  bm.removeByRight("one");

  EXPECT_FALSE(bm.getRight(1).has_value());
  EXPECT_FALSE(bm.getLeft("one").has_value());
  EXPECT_EQ(bm.size(), 1u);
}

// -----------------------------------------------------------------------
// clear
// -----------------------------------------------------------------------

TEST(Bimap, ClearEmptiesMap)
{
  core::Bimap<int, std::string> bm;
  bm.insert(1, "one");
  bm.insert(2, "two");

  bm.clear();

  EXPECT_EQ(bm.size(), 0u);
  EXPECT_FALSE(bm.getRight(1).has_value());
  EXPECT_FALSE(bm.getLeft("one").has_value());
}

// -----------------------------------------------------------------------
// Works with non-string types
// -----------------------------------------------------------------------

TEST(Bimap, IntToFloatMapping)
{
  core::Bimap<int, double> bm;
  bm.insert(42, 3.14);

  ASSERT_TRUE(bm.getRight(42).has_value());
  EXPECT_DOUBLE_EQ(*bm.getRight(42), 3.14);

  ASSERT_TRUE(bm.getLeft(3.14).has_value());
  EXPECT_EQ(*bm.getLeft(3.14), 42);
}
