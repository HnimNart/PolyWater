#include "entrypoints/raytrace/ray_common.h.slang"
#include "include/bsdf/mirror.h.slang"

/**********************************************************/
[shader("closesthit")] 
void rchit_mirror(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
    HitState hit = getHitState(attr);
    const Material material = hit.material;

    float3 albedo = material.baseColorFactor.xyz;
    MirrorBSDF bsdf = MirrorBSDF();
    float3 bounceDir;
    
    payload.weight *= albedo * bsdf.sample(hit.viewDir, hit.worldNormal, payload.seed, bounceDir);

    // --- First-hit auxiliary data for denoiser ---
    if (payload.depth == 0)
    {
        payload.albedo = albedo;
        payload.normal = hit.worldNormal;
    }
    payload.nextRayOrigin = hit.worldPos;
    payload.nextRayDir = normalize(bounceDir);
    payload.stop = false;
    payload.emit = true;
}


