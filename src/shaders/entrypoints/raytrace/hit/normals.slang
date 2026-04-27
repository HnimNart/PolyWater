#include "entrypoints/raytrace/ray_common.h.slang"

//-----------------------------------------------------------------------
// NORMAL VISUALIZER CLOSEST HIT SHADER
//-----------------------------------------------------------------------
/**********************************************************/
[shader("closesthit")]
void rchit_normals(inout HitPayload payload,
                   in BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
    HitState hit = getHitState(attr);

    float3 normalColor = hit.worldNormal * 0.5 + 0.5;
    // In a path tracer, visualization shaders usually stop the ray immediately
    payload.color = normalColor;
    payload.weight = float3(0.0); 
    payload.stop = true;          
}
