#ifndef MATH_H
#define MATH_H 1

// Use IFloat to ensure this works for float, float2, float3, etc.
T safe_div<T : IFloat>(T x, T y)
{
  T min_val = T(1e-6f);
  return x / (y < min_val ? min_val : y);
}

#endif
