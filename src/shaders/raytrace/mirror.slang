#include "shared.h.slang"

struct MirrorBSDF : IBSDF {

    float3 eval(float3 L, float3 N) { return 0.0f; }

    float3 sample(float3 vDir, float3 worldNormal, inout uint seed, inout float3 outDir) {
        if (dot(vDir, worldNormal) < 0) {
            return float3(0.0f);
        }
        
        outDir = reflect(-vDir, worldNormal); 
        return float3(1.0f);
    }
}

[shader("closesthit")] 
void rchit_mirror(inout HitPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    HitState hit = getHitState(attr);

    MirrorBSDF bsdf = MirrorBSDF();
    float3 bounceDir;
    
    payload.weight *= bsdf.sample(hit.viewDir, hit.worldNormal, payload.seed, bounceDir);
    payload.nextRayOrigin = hit.worldPos;
    payload.nextRayDir = normalize(bounceDir);
    payload.stop = false;
    payload.emit = true;
}


