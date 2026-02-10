
#ifndef SAMPLER_H_SLANG
#define SAMPLER_H_SLANG 1

#include "math.h.slang"
#include "rnd.h.slang"
// -----------------------------------------------------------------------
// Generates a Cosine-Weighted random direction
// Returns: A normalized World Space direction centered around 'normal'
// -----------------------------------------------------------------------
float3 sampleCosineWeightedHemisphere(float3 normal, inout uint seed)
{
  // A. Generate Random Numbers
  float r1 = rnd(seed);
  float r2 = rnd(seed);

  // B. Calculate Local Cosine Sample
  // Optimization: We can avoid expensive acos() and sin() calls
  // because sin^2 + cos^2 = 1.
  float phi = 2.0 * 3.14159265 * r1;
  float cosTheta = sqrt(r2);        // z component
  float sinTheta = sqrt(1.0 - r2);  // radius on xy plane

  float3 localDir;
  localDir.x = sinTheta * cos(phi);
  localDir.y = sinTheta * sin(phi);
  localDir.z = cosTheta;

  // C. Transform Local -> World Space
  float3 tangent, bitangent;
  createCoordinateSystem(normal, tangent, bitangent);

  return localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
}

#endif
