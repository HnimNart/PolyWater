#ifndef I_BSDF_H
#define I_BSDF_H 1

interface IBSDF
{
  float3 eval(float3 lightDir, float3 shadingNormal);
}

#endif
