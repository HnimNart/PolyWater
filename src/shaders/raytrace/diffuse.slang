#include "shared.h.slang"

struct DiffuseEvaluator : IBSDF
{
  // This is the Slang equivalent of a constructor
  __init(float3 _albedo) { this.albedo = _albedo; }
  float3 albedo;

  // The required function
  float3 eval(float3 L) { return albedo / M_PI; }
}

    //-----------------------------------------------------------------------
    // DIFFUSE CLOSEST HIT SHADER - Path Tracing + NEE
    //-----------------------------------------------------------------------
    [shader("closesthit")] void
    rchit_diffuse(inout HitPayload payload,
                  in BuiltInTriangleIntersectionAttributes attr)
{
  HitState hit = getHitState(attr);

  const float3 worldPos = hit.worldPos;
  const float3 worldNormal = hit.worldNormal;
  const float3 viewDir = hit.viewDir;
  const float2 uv = hit.uv;
  const Material material = hit.material;

  // --- 4. MATERIAL BASE COLOR (ALBEDO) ---
  float3 albedo = material.baseColorFactor.xyz;
  if (material.baseColorTextureIndex > 0)
  {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(uv, 0).xyz;
  }

  // --- 5. EMISSIVE ACCUMULATION ---
  float3 emissive = material.emission;
  payload.color += emissive * payload.weight;

  // --- 6. NEXT EVENT ESTIMATION (Direct Lighting) ---
  const SceneInfo* sceneInfo = pushConst.sceneInfoAddress;
  DiffuseEvaluator bsdf = DiffuseEvaluator(albedo);
  payload.color +=
      sampleDirectLighting(sceneInfo, worldPos, worldNormal, bsdf) *
      payload.weight;

  // --- 7. INDIRECT BOUNCE (Diffuse Only) ---
  // Simple cosine-weighted scatter
  float3 bounceDir = sampleCosineWeightedHemisphere(worldNormal, payload.seed);
  payload.weight *= albedo;  // PDF of cosine is M_PIf / cos(theta)

  // --- 8. PREPARE NEXT RAY ---
  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(bounceDir);
  payload.stop = false;
}
