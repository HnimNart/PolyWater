#include <gtest/gtest.h>

#include <string>

#include "string_utils.h"

// -----------------------------------------------------------------------
// capitalize
// -----------------------------------------------------------------------

TEST(StringUtils_Capitalize, EmptyStringIsUnchanged)
{
  EXPECT_EQ(core::capitalize(""), "");
}

TEST(StringUtils_Capitalize, LowercaseFirstChar)
{
  EXPECT_EQ(core::capitalize("hello"), "Hello");
}

TEST(StringUtils_Capitalize, AlreadyCapitalized)
{
  EXPECT_EQ(core::capitalize("World"), "World");
}

TEST(StringUtils_Capitalize, SingleChar)
{
  EXPECT_EQ(core::capitalize("a"), "A");
}

TEST(StringUtils_Capitalize, DoesNotChangeRestOfString)
{
  EXPECT_EQ(core::capitalize("hELLO"), "HELLO");
}

// -----------------------------------------------------------------------
// toLower
// -----------------------------------------------------------------------

TEST(StringUtils_ToLower, AllUppercase)
{
  std::string s = "HELLO";
  core::toLower(s);
  EXPECT_EQ(s, "hello");
}

TEST(StringUtils_ToLower, MixedCase)
{
  std::string s = "HeLLo WoRLd";
  core::toLower(s);
  EXPECT_EQ(s, "hello world");
}

TEST(StringUtils_ToLower, AlreadyLowercase)
{
  std::string s = "already";
  core::toLower(s);
  EXPECT_EQ(s, "already");
}

TEST(StringUtils_ToLower, EmptyString)
{
  std::string s;
  core::toLower(s);
  EXPECT_EQ(s, "");
}

// -----------------------------------------------------------------------
// trim
// -----------------------------------------------------------------------

TEST(StringUtils_Trim, LeadingAndTrailingSpaces)
{
  EXPECT_EQ(core::trim("  hello  "), "hello");
}

TEST(StringUtils_Trim, TabsAndNewlines)
{
  EXPECT_EQ(core::trim("\t\nhello\r\n"), "hello");
}

TEST(StringUtils_Trim, NoWhitespace)
{
  EXPECT_EQ(core::trim("hello"), "hello");
}

TEST(StringUtils_Trim, OnlyWhitespace)
{
  EXPECT_EQ(core::trim("   \t  "), "");
}

TEST(StringUtils_Trim, EmptyString)
{
  EXPECT_EQ(core::trim(""), "");
}

TEST(StringUtils_Trim, InternalWhitespaceUntouched)
{
  EXPECT_EQ(core::trim("  hello world  "), "hello world");
}

// -----------------------------------------------------------------------
// getDirectory / getFilename / getExtension / getLowercasedStem
// -----------------------------------------------------------------------

TEST(StringUtils_GetDirectory, TypicalPath)
{
  EXPECT_EQ(core::getDirectory("assets/models/helmet.gltf"), "assets/models");
}

TEST(StringUtils_GetDirectory, FileInRoot)
{
  // A bare filename has an empty parent
  EXPECT_EQ(core::getDirectory("file.txt"), "");
}

TEST(StringUtils_GetFilename, ExtractsFilename)
{
  EXPECT_EQ(core::getFilename("assets/models/helmet.gltf"), "helmet.gltf");
}

TEST(StringUtils_GetFilename, BareFilename)
{
  EXPECT_EQ(core::getFilename("file.txt"), "file.txt");
}

TEST(StringUtils_GetExtension, CommonExtension)
{
  EXPECT_EQ(core::getExtension("model.gltf"), ".gltf");
}

TEST(StringUtils_GetExtension, NoExtension)
{
  EXPECT_EQ(core::getExtension("Makefile"), "");
}

TEST(StringUtils_GetExtension, MultiDot)
{
  EXPECT_EQ(core::getExtension("archive.tar.gz"), ".gz");
}

TEST(StringUtils_GetLowercasedStem, MixedCase)
{
  EXPECT_EQ(core::getLowercasedStem("assets/Helmet.GLTF"), "helmet");
}

TEST(StringUtils_GetLowercasedStem, AlreadyLowercase)
{
  EXPECT_EQ(core::getLowercasedStem("model.gltf"), "model");
}

TEST(StringUtils_GetFileName, SameAsGetFilename)
{
  // getFileName and getFilename should behave identically
  std::string path = "assets/models/scene.obj";
  EXPECT_EQ(core::getFileName(path), core::getFilename(path));
}
