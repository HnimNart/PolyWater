#ifndef MIRROR_H_SLANG
#define MIRROR_H_SLANG 1

#include "ibsdf.h.slang"

struct MirrorBSDF : IBSDF {

    /**********************************************************/
    float3 eval(float3 L, float3 N)
    /**********************************************************/
    {
      return 0.0f;
    }

    /**********************************************************/
    float3 sample(float3 vDir, float3 worldNormal, inout uint seed, inout float3 outDir)
    /**********************************************************/
    {
        if (dot(vDir, worldNormal) < 0) {
            return float3(0.0f);
        }
        
        outDir = reflect(-vDir, worldNormal); 
        return float3(1.0f);
    }
}

#endif
