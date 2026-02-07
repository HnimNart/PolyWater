#ifndef MATH_H
#define MATH_H 1

// Use IFloat to ensure this works for float, float2, float3, etc.
T safe_div<T : IFloat>(T x, T y)
{
  T min_val = T(1e-6f);
  return x / (y < min_val ? min_val : y);
}

// -----------------------------------------------------------------------
// Helper: Builds an Orthonormal Basis (Tangent Space) from a Normal
// Duff et al. method - robust and singularity-free
// -----------------------------------------------------------------------
void createCoordinateSystem(in float3 N, out float3 T, out float3 B)
{
  float sign = N.z >= 0.0f ? 1.0f : -1.0f;
  float a = -1.0f / (sign + N.z);
  float b = N.x * N.y * a;

  T = float3(1.0f + sign * N.x * N.x * a, sign * b, -sign * N.x);
  B = float3(b, sign + N.y * N.y * a, -N.y);
}

#endif
