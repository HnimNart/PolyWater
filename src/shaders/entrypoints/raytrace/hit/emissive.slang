#include "entrypoints/raytrace/ray_common.h.slang"

/**********************************************************/
[shader("closesthit")] void
rchit_emissive(inout HitPayload payload,
               in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
    HitState hit = getHitState(attr);

    float dotVN = dot(hit.viewDir, hit.worldNormal);
    if (dotVN > 0.0f) {
        float3 emission = hit.material.emission.xyz;
        payload.color += emission * payload.weight;
    } 

    if (payload.depth == 0)
    {
        payload.albedo = hit.material.emission.xyz;
        payload.normal = hit.worldNormal;
    }

    payload.stop = true;
    payload.emit = false; 
}
