#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "discrete_pdf.hpp"

// -----------------------------------------------------------------------
// Construction / build
// -----------------------------------------------------------------------

TEST(DiscretePDF, BuildNormalisesToOne)
{
  DiscretePDF pdf({1.0f, 2.0f, 3.0f, 4.0f});

  // Sum of normalised PMF must equal 1
  float sum = 0.0f;
  for (float p : pdf.getPmf())
    sum += p;
  EXPECT_NEAR(sum, 1.0f, 1e-5f);

  // CDF last element must equal 1
  EXPECT_NEAR(pdf.getCdf().back(), 1.0f, 1e-5f);

  EXPECT_NEAR(pdf.getTotalSum(), 10.0f, 1e-5f);
}

TEST(DiscretePDF, EqualWeightsUniformPMF)
{
  const int n = 5;
  DiscretePDF pdf(std::vector<float>(n, 1.0f));

  for (int i = 0; i < n; ++i)
    EXPECT_NEAR(pdf.getPDF(i), 1.0f / n, 1e-5f);
}

// -----------------------------------------------------------------------
// getPDF – boundary indices
// -----------------------------------------------------------------------

TEST(DiscretePDF, OutOfBoundsIndexReturnsZero)
{
  DiscretePDF pdf({1.0f, 2.0f, 3.0f});

  EXPECT_FLOAT_EQ(pdf.getPDF(-1), 0.0f);
  EXPECT_FLOAT_EQ(pdf.getPDF(3), 0.0f);
  EXPECT_FLOAT_EQ(pdf.getPDF(100), 0.0f);
}

// -----------------------------------------------------------------------
// sample – basic correctness
// -----------------------------------------------------------------------

TEST(DiscretePDF, SampleReturnsValidIndex)
{
  DiscretePDF pdf({1.0f, 1.0f, 1.0f});

  for (float u : {0.0f, 0.1f, 0.5f, 0.9f, 0.9999f})
  {
    float p;
    int idx = pdf.sample(u, p);
    EXPECT_GE(idx, 0) << "  u=" << u;
    EXPECT_LT(idx, 3) << "  u=" << u;
    EXPECT_GT(p, 0.0f) << "  u=" << u;
  }
}

TEST(DiscretePDF, SamplePDFMatchesGetPDF)
{
  DiscretePDF pdf({1.0f, 3.0f, 2.0f});

  float p;
  int idx = pdf.sample(0.0f, p);

  EXPECT_NEAR(p, pdf.getPDF(idx), 1e-6f);
}

TEST(DiscretePDF, SampleBiasedTowardHighWeight)
{
  // weight[2] is 90% of total, so sampling u in (0.1, 1) should almost always
  // pick index 2.
  DiscretePDF pdf({1.0f, 0.0f, 9.0f});  // pmf = [0.1, 0, 0.9]

  float p;
  int idx = pdf.sample(0.5f, p);  // u=0.5 falls in the [0.1,1.0] bucket
  EXPECT_EQ(idx, 2);
}

// -----------------------------------------------------------------------
// Edge case: zero-weight distribution
// -----------------------------------------------------------------------

TEST(DiscretePDF, AllZeroWeightsReturnsMinus1)
{
  DiscretePDF pdf({0.0f, 0.0f, 0.0f});

  float p = 0.0f;
  int idx = pdf.sample(0.5f, p);
  EXPECT_EQ(idx, -1);
  EXPECT_FLOAT_EQ(pdf.getTotalSum(), 0.0f);
}

// -----------------------------------------------------------------------
// Single element
// -----------------------------------------------------------------------

TEST(DiscretePDF, SingleElementAlwaysPicked)
{
  DiscretePDF pdf({5.0f});

  float p;
  int idx = pdf.sample(0.0f, p);
  EXPECT_EQ(idx, 0);
  EXPECT_NEAR(p, 1.0f, 1e-5f);
}
