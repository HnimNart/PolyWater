#ifndef DIFFUSE_H_SLANG
#define DIFFUSE_H_SLANG 1

#include "ibsdf.h.slang"

struct DiffuseEvaluator : IBSDF {
  __init(float3 albedo)
  {
    this.albedo = albedo;
  }
  float3 eval(float3 L, float3 N)
  {
    float nDotL = max(0.0, dot(N, L));
    return albedo * M_1_PI * nDotL;
  }

  float3 sample(float3 viewDir, float3 worldNormal, inout int seed,
                inout float3 outDir)
  {
    outDir = sampleCosineWeightedHemisphere(worldNormal, seed);
    return albedo;
  }

  float3 albedo;
}


#endif
