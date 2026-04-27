#pragma once

#include <algorithm>
#include <numeric>
#include <vector>

class DiscretePDF
{
public:
  DiscretePDF(const std::vector<float>& weights) : sum(0.0f) { build(weights); }

  // 1. Initialize with a list of weights (e.g., Light Power)
  void build(const std::vector<float>& weights)
  {
    size_t n = weights.size();
    cdf.resize(n + 1);
    pmf.resize(n);

    cdf[0] = 0.0f;
    for (size_t i = 0; i < n; ++i)
    {
      pmf[i] = weights[i];
      cdf[i + 1] = cdf[i] + weights[i];
    }

    sum = cdf[n];

    // Normalize the CDF so the last element is 1.0
    if (sum > 0.0f)
    {
      for (float& val : cdf)
        val /= sum;
      for (float& val : pmf)
        val /= sum;
    }
  }

  // 2. Pick an index based on a uniform random number u [0, 1)
  int sample(float u, float& pdf) const
  {
    if (sum <= 0.0f)
      return -1;

    // Binary search to find the first element > u
    auto it = std::upper_bound(cdf.begin(), cdf.end(), u);
    int index = std::clamp(static_cast<int>(std::distance(cdf.begin(), it) - 1),
                           0, static_cast<int>(pmf.size() - 1));

    pdf = pmf[index];
    return index;
  }

  // 3. Query the PDF of a specific index (useful for MIS)
  float getPDF(int index) const
  {
    if (index < 0 || index >= pmf.size())
      return 0.0f;
    return pmf[index];
  }

  float getTotalSum() const { return sum; }
  const std::vector<float>& getCdf() const { return cdf; }
  const std::vector<float>& getPmf() const { return pmf; }

private:
  std::vector<float> cdf;  // Cumulative Distribution Function
  std::vector<float> pmf;  // Probability Mass Function (normalized weights)
  float sum;               // Total unnormalized weight
};
