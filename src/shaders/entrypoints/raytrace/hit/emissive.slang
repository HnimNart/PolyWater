#include "entrypoints/raytrace/ray_common.h.slang"

/**********************************************************/
[shader("closesthit")] void
rchit_emissive(inout HitPayload payload,
               in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
    HitState hit = getHitState(attr);

    // 1. Check if we hit the front face
    float dotVN = dot(hit.viewDir, hit.worldNormal);
    if (dotVN > 0.0f) {
        float3 emission = hit.material.emission.xyz;
        payload.color += emission * payload.weight;
    } 

    // --- First-hit auxiliary data for denoiser ---
    if (payload.depth == 0)
    {
        payload.albedo = hit.material.emission.xyz;
        payload.normal = hit.worldNormal;
    }

    payload.stop = true;
    payload.emit = false; 
}
