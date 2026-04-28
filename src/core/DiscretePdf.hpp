#pragma once

#include <algorithm>
#include <numeric>
#include <vector>

class DiscretePDF
{
public:
  DiscretePDF(const std::vector<float>& weights);

  // 1. Initialize with a list of weights (e.g., Light Power)
  void build(const std::vector<float>& weights);

  // 2. Pick an index based on a uniform random number u [0, 1)
  int sample(float u, float& pdf) const;

  // 3. Query the PDF of a specific index (useful for MIS)
  float getPDF(int index) const
  {
    if (index < 0 || index >= pmf.size())
      return 0.0f;
    return pmf[index];
  }

  float getTotalSum() const
  {
    return sum;
  }
  const std::vector<float>& getCdf() const
  {
    return cdf;
  }
  const std::vector<float>& getPmf() const
  {
    return pmf;
  }

private:
  std::vector<float> cdf;  // Cumulative Distribution Function
  std::vector<float> pmf;  // Probability Mass Function (normalized weights)
  float sum;               // Total unnormalized weight
};
