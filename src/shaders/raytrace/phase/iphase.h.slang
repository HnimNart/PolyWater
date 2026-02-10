#ifndef _IPHASE_FUNC_H_SLANG
#define _IPHASE_FUNC_H_SLANG 1

interface IPhaseFunc
{
    float3 eval(float3 in, float3 out);
    float3 sample(float3 in, inout int seed);
}

#endif
