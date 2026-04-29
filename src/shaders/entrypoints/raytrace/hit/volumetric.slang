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

    // Material Properties
    float ior = material.ior.x;
    float3 albedo = material.baseColorFactor.xyz; 
    float g = material.asymmetry.x;
    float3 sigma_t = material.sigma_t;
    float3 sigma_s = albedo * sigma_t; 
    float3 sigma_a = (1.0 - albedo) * sigma_t; 

    // Prepare Dielectric Surface Evaluator
    DielectricEvaluator bsdf = DielectricEvaluator(float3(1.0), ior); 
    float3 N = worldNormal;
    float cosThetaI = dot(N, viewDir);
    
    // Check if we are inside (Backface Hit)
    bool isInside = cosThetaI < 0.0; 
    
    // --- Volumetric Logic ---
    if (isInside) {
        float hitDistance = RayTCurrent();
        
        // 1. Calculate Average Extinction for Sampling
        float avgSigmaT = (sigma_t.x + sigma_t.y + sigma_t.z) / 3.0;
        
        // 2. Sample Distance (Importance Sampling)
        float scatterDistance = safe_div(-log(rnd(payload.seed)), avgSigmaT);

        if (scatterDistance < hitDistance) {
            // --- SCATTER EVENT ---
            float3 scatterPos = WorldRayOrigin() + WorldRayDirection() * scatterDistance;
            float3 chromaticCorrection = exp(-(sigma_t - avgSigmaT) * scatterDistance);

            payload.weight *= (sigma_s / max(1e-5, avgSigmaT)) * chromaticCorrection;
            
            // Phase Function
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

    // --- Surface Logic ---
    float3 nextDir;
    float3 surfaceWeight = bsdf.sample(viewDir, worldNormal, payload.seed, nextDir);
    
    // --- First-hit auxiliary data for denoiser ---
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
