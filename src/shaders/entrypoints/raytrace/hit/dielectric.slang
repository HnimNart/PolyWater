
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

  float3 nextDir;
  payload.weight *= bsdf.sample(viewDir, worldNormal, payload.seed, nextDir);

  if (payload.depth == 0)
  {
    payload.albedo = albedo;
    payload.normal = worldNormal;
  }

  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = normalize(nextDir);

  payload.stop = false;
  payload.emit = true;
}
