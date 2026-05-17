#include "entrypoints/raytrace/ray_common.h.slang"

/**********************************************************/
[shader("closesthit")]
void rchit_normals(inout HitPayload payload,
                   in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
    HitState hit = getHitState(attr);

    float3 normalColor = hit.worldNormal * 0.5 + 0.5;
    payload.color = normalColor;
    payload.weight = float3(0.0); 
    payload.stop = true;
    if (payload.depth == 0)
    {
        payload.albedo = normalColor;
        payload.normal = hit.worldNormal;
    }
}
