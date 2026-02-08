#include "shared.h.slang"

struct DielectricEvaluator : IBSDF {
  float ior;
  float3 albedo;

  __init(float3 albedo, float ior) {
    this.albedo = albedo;
    this.ior = ior;
  }

  // Dielectrics are usually handled via delta distributions (perfect smooth),
  // so eval() is often zero for importance sampling.
  float3 eval(float3 L, float3 N) { return float3(0.0); }

  float3 sample(float3 viewDir, float3 worldNormal, inout int seed,
                inout float3 outDir) {
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

    float randomVal = rnd(seed);
    if (randomVal < fresnel) {
      // Reflection
      outDir = reflect(-viewDir, N);
      return albedo;
    } else {
      // Refraction (Snell's Law)
      float3 refractDir = refract(-viewDir, N, eta);
      // Check for Total Internal Reflection (TIR)
      if (length(refractDir) < 0.01) {
        outDir = reflect(-viewDir, N);
        return albedo;
      }
      outDir = refractDir;
      return albedo;
    }
  }
}

    [shader("closesthit")] void
    rchit_dielectric(inout HitPayload payload,
                     in BuiltInTriangleIntersectionAttributes attr) {
  HitState hit = getHitState(attr);

  const float3 worldPos = hit.worldPos;
  const float3 worldNormal = hit.worldNormal;
  const float3 viewDir = hit.viewDir;
  const Material material = hit.material;

  float ior = material.ior.x;
  float3 albedo = material.baseColorFactor.xyz;
  DielectricEvaluator bsdf = DielectricEvaluator(albedo, ior);

  // 1. Indirect Bounce (Reflect or Refract)
  float3 nextDir;
  payload.weight *= bsdf.sample(viewDir, worldNormal, payload.seed, nextDir);

  // 2. Prepare Next Ray
  // Offset origin along the normal to prevent self-intersection (bias)
  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(nextDir);

  payload.stop = false;
  payload.emit = true;
}
