#include "entrypoints/raytrace/ray_common.h.slang"
#include "include/phase/hg.h.slang"
#include "include/bsdf/dielectric.h.slang"

/**********************************************************/
[shader("closesthit")]
void rchit_volumetric(inout HitPayload payload, 
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
    float g = material.asymmetry.x;
    float3 sigma_t = material.sigma_t;
    float3 sigma_s = albedo * sigma_t; 
    float3 sigma_a = (1.0 - albedo) * sigma_t; 

    DielectricEvaluator bsdf = DielectricEvaluator(float3(1.0), ior); 
    float3 N = worldNormal;
    float cosThetaI = dot(N, viewDir);
    
    bool isInside = cosThetaI < 0.0; 
    
    if (isInside) {
        float hitDistance = RayTCurrent();
        
        float avgSigmaT = (sigma_t.x + sigma_t.y + sigma_t.z) / 3.0;
        
        float scatterDistance = safe_div(-log(rnd(payload.seed)), avgSigmaT);

        if (scatterDistance < hitDistance) {
            float3 scatterPos = WorldRayOrigin() + WorldRayDirection() * scatterDistance;
            float3 chromaticCorrection = exp(-(sigma_t - avgSigmaT) * scatterDistance);

            payload.weight *= (sigma_s / max(1e-5, avgSigmaT)) * chromaticCorrection;
            
            HenyeyGreensteinPhase phase = HenyeyGreensteinPhase(g);
            float3 scatteredDir = phase.sample(viewDir, payload.seed);
            
            payload.nextRayOrigin = scatterPos;
            payload.nextRayDir = normalize(scatteredDir);
            payload.stop = false;
            payload.emit = true;
            
            return; 
        } else {
            float3 chromaticCorrection = exp(-(sigma_t - avgSigmaT) * hitDistance);
            payload.weight *= chromaticCorrection;
        }
    }

    float3 nextDir;
    float3 surfaceWeight = bsdf.sample(viewDir, worldNormal, payload.seed, nextDir);
    
    if (payload.depth == 0)
    {
        payload.albedo = albedo;
        payload.normal = worldNormal;
    }

    payload.weight *= surfaceWeight;
    payload.nextRayOrigin = worldPos;
    payload.nextRayDir = normalize(nextDir);
    payload.stop = false;
    payload.emit = true;
}
