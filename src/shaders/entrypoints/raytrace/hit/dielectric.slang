
#include "entrypoints/raytrace/ray_common.h.slang"
#include "include/bsdf/dielectric.h.slang"

/**********************************************************/
[shader("closesthit")] void
rchit_dielectric(inout HitPayload payload,
                 in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
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

  // --- First-hit auxiliary data for denoiser ---
  if (payload.depth == 0)
  {
    payload.albedo = albedo;
    payload.normal = worldNormal;
  }

  // 2. Prepare Next Ray
  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(nextDir);

  payload.stop = false;
  payload.emit = true;
}
