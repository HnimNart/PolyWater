#include "entrypoints/raytrace/ray_common.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "include/bsdf/diffuse.h.slang"

//-----------------------------------------------------------------------
// CLOSEST HIT SHADER - PBR Path Tracer + NEE
//-----------------------------------------------------------------------
[shader("closesthit")] void
rchit_gltf(inout HitPayload payload,
           in BuiltInTriangleIntersectionAttributes attr) {
  HitState hit = getHitState(attr);

  const float3 worldPos = hit.worldPos;
  const float3 worldNormal = hit.worldNormal;
  const float3 viewDir = hit.viewDir;
  const float2 uv = hit.uv;
  const Material material = hit.material;

  // --- GATHER MATERIAL PROPERTIES ---
  float3 albedo = material.baseColorFactor.xyz;
  float metallic = material.metallicFactor;
  float roughness = material.roughnessFactor;
  float3 emissive = material.emission;

  // Textures
  if (material.baseColorTextureIndex > 0) {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(uv, 0).xyz;
  }
  payload.color += emissive * payload.weight;

  // --- (Direct Lighting) ---
  PbrEvaluator bsdf = PbrEvaluator(viewDir, albedo, roughness, metallic);
  const SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  payload.color += sampleDirectLighting(sceneInfo, worldPos, worldNormal, bsdf, payload.seed) *
                   payload.weight;

  // --- (Indirect Lighting) ---
  float3 bounceDir;
  payload.weight *= bsdf.sample(viewDir, worldNormal, payload.seed, bounceDir);

  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = bounceDir;
  payload.stop = false;
  payload.emit = false;
}
