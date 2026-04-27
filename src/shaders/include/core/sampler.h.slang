
#ifndef SAMPLER_H_SLANG
#define SAMPLER_H_SLANG 1

#include "math.h.slang"
#include "rnd.h.slang"
#include "functions.h.slang"

// -----------------------------------------------------------------------
// Generates a Cosine-Weighted random direction
// Returns: A normalized World Space direction centered around 'normal'
// -----------------------------------------------------------------------
/**********************************************************/
float3 sampleCosineWeightedHemisphere(float3 normal, inout uint seed)
/**********************************************************/
{
  // A. Generate Random Numbers
  float r1 = rnd(seed);
  float r2 = rnd(seed);

  // B. Calculate Local Cosine Sample using the Ray Tracing Gems method
  float3 localDir = cosineSampleHemisphere(r1, r2);

  // C. Transform Local -> World Space
  float3 tangent, bitangent;
  orthonormalBasis(normal, tangent, bitangent);

  return localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
}

//-----------------------------------------------------------------------
// https://www.realtimerendering.com/raytracinggems/unofficial_RayTracingGems_v1.4.pdf
// 16.6.1 COSINE-WEIGHTED HEMISPHERE ORIENTED TO THE Z-AXIS
//-----------------------------------------------------------------------
/**********************************************************/
inline float3 cosineSampleHemisphere(float r1, float r2)
/**********************************************************/
{
  float  r   = sqrt(r1);
  float  phi = M_TWO_PI * r2;
  float3 dir;
  dir.x = r * cos(phi);
  dir.y = r * sin(phi);
  dir.z = sqrt(max(0.F, 1.F - dir.x * dir.x - dir.y * dir.y));
  return dir;
}

#endif
