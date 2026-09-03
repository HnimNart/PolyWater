#ifndef DIELECTRIC_H_SLANG
#define DIELECTRIC_H_SLANG 1

#include "ibsdf.h.slang"

struct DielectricEvaluator : IBSDF {
  float ior;
  float3 albedo;

  __init(float3 albedo, float ior)
  {
    this.albedo = albedo;
    this.ior = ior;
  }

  float3 eval(float3 L, float3 N)
  {
    return float3(0.0);
  }

  float3 sample(float3 viewDir, float3 worldNormal, inout int seed,
                inout float3 outDir)
  {
    float3 N = worldNormal;
    float eta = 1.0 / ior;

    float cosThetaI = dot(N, viewDir);

    // Handle rays coming from inside the object
    if (cosThetaI < 0.0) {
      cosThetaI = -cosThetaI;
      N = -N;
      eta = ior;
    }

    // Schlick's Approximation for Fresnel
    float r0 = (1.0 - eta) / (1.0 + eta);
    r0 = r0 * r0;
    float fresnel = r0 + (1.0 - r0) * pow(1.0 - cosThetaI, 5.0);

    float maxVal = max(albedo.x, max(albedo.y, albedo.z));
    float3 weight = maxVal <= 0 ? float3(1.0f) : albedo;

    float randomVal = rnd(seed);
    if (randomVal < fresnel) {
      outDir = reflect(-viewDir, N);
      return weight;
    } else {
      float3 refractDir = refract(-viewDir, N, eta);
      if (length(refractDir) < 0.01) {
        outDir = reflect(-viewDir, N);
        return weight;
      }
      outDir = refractDir;
      return weight;
    }
  }
}

#endif
