#ifndef _HG_H_SLANG_
#define _HG_H_SLANG_ 1

#include "iphase.h.slang"

#include "include/core/math.h.slang"

// --- 1. Henyey-Greenstein Phase Function ---
struct HenyeyGreensteinPhase : IPhaseFunc {
    float g; // Anisotropy (-1.0 to 1.0)

    __init(float g)
    {
        this.g = g;
    }

    float3 eval(float3 inDir, float3 outDir)
    {
        float cosTheta = dot(-inDir, outDir);
        float denom = 1.0 + g * g - 2.0 * g * cosTheta;
        return float3((1.0 - g * g) / (4.0 * M_PI * pow(max(0.001, denom), 1.5)));
    }

    float3 sample(float3 inDir, inout int seed)
    {
        float2 u = float2(rnd(seed), rnd(seed));
        float cosTheta;

        if (abs(g) < 1e-3) {
            cosTheta = 1.0 - 2.0 * u.x;
        } else {
            float sqrTerm = (1.0 - g * g) / (1.0 - g + 2.0 * g * u.x);
            cosTheta = (1.0 + g * g - sqrTerm * sqrTerm) / (2.0 * g);
        }

        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
        float phi = 2.0 * M_PI * u.y;

        float3 localDir = float3(
            sinTheta * cos(phi),
            sinTheta * sin(phi),
            cosTheta
        );

        // Transform from local space (where Z is -inDir) to world space
        float3 T, B;
        orthonormalBasis(-inDir, T, B);
        return localDir.x * T + localDir.y * B + localDir.z * -inDir;
    }
}

#endif
