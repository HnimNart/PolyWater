#include "shared.h.slang"

struct DiffuseEvaluator : IBSDF {
  __init(float3 albedo) { this.albedo = albedo; }
  float3 eval(float3 L, float3 N) {
    float nDotL = max(0.0, dot(N, L));
    return albedo * M_1_PI * nDotL;
  }

  float3 sample(float3 viewDir, float3 worldNormal, inout int seed,
                inout float3 outDir) {
    outDir = sampleCosineWeightedHemisphere(worldNormal, seed);
    return albedo;
  }

  float3 albedo;
}

    //-----------------------------------------------------------------------
    // DIFFUSE CLOSEST HIT SHADER - Path Tracing + NEE
    //-----------------------------------------------------------------------
    [shader("closesthit")] void
    rchit_diffuse(inout HitPayload payload,
                  in BuiltInTriangleIntersectionAttributes attr) {
  HitState hit = getHitState(attr);

  const float3 worldPos = hit.worldPos;
  const float3 worldNormal = hit.worldNormal;
  const float3 viewDir = hit.viewDir;
  const float2 uv = hit.uv;
  const Material material = hit.material;

  float3 albedo = material.baseColorFactor.xyz;
  if (material.baseColorTextureIndex > 0) {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(uv, 0).xyz;
  }

  // --- 5. EMISSIVE ACCUMULATION ---
  float3 emissive = material.emission;
  payload.color += emissive * payload.weight;

  // --- 6. NEXT EVENT ESTIMATION (Direct Lighting) ---
  const SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  DiffuseEvaluator bsdf = DiffuseEvaluator(albedo);
  payload.color +=
      sampleDirectLighting(sceneInfo, worldPos, worldNormal, bsdf) *
      payload.weight;

  // --- 7. INDIRECT BOUNCE (Diffuse Only) ---
  // Simple cosine-weighted scatter
  float3 bounceDir;
  payload.weight *= bsdf.sample(viewDir, worldNormal, payload.seed, bounceDir);

  // --- 8. PREPARE NEXT RAY ---
  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(bounceDir);
  payload.stop = false;
  payload.emit = false;
}
