#include "DiscretePdf.hpp"

#include <algorithm>
#include <numeric>

DiscretePDF::DiscretePDF(const std::vector<float>& weights) : sum(0.0f)
{
  build(weights);
}

void DiscretePDF::build(const std::vector<float>& weights)
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

int DiscretePDF::sample(float u, float& pdf) const
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
