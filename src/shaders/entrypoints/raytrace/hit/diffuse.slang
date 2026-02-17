#include "entrypoints/raytrace/ray_common.h.slang"
#include "include/bsdf/diffuse.h.slang"

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

  // --- EMISSIVE ACCUMULATION ---
  float3 emissive = material.emission;
  payload.color += emissive * payload.weight;

  // --- Direct Lighting ---
  const SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  DiffuseEvaluator bsdf = DiffuseEvaluator(albedo);
  payload.color +=
      sampleDirectLighting(sceneInfo, worldPos, worldNormal, bsdf) *
      payload.weight;

  // --- INDIRECT BOUNCE ---
  float3 bounceDir;
  payload.weight *= bsdf.sample(viewDir, worldNormal, payload.seed, bounceDir);

  // --- PREPARE NEXT RAY ---
  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(bounceDir);
  payload.stop = false;
  payload.emit = false;
}
