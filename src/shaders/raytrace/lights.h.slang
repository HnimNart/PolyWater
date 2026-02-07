#ifndef RAYTRACE_LIGTHS_H_SLANG
#  define RAYTRACE_LIGHTS_H_SLANG 1

#  include "bsdf/ibsdf.h.slang"
#  include "ray_common.h.slang"

//-----------------------------------------------------------------------
// DIRECT LIGHTING LOOP
//-----------------------------------------------------------------------

float3 sampleDirectLighting(SceneInfo* sceneInfo, float3 worldPos,
                            float3 worldNormal, IBSDF bsdf)
{
  float3 color = float3(0);
  // Loop over all lights
  for (int i = 0; i < sceneInfo->numLights; i++)
  {
    LightSample light = sampleLight(sceneInfo, worldPos, i);

    if (dot(worldNormal, light.direction) > 0.0)
    {
      // C. Shadow Test
      float distToLight = length(light.position - worldPos);
      float visibility = testShadow(worldPos, light.direction, distToLight);

      // D. If Visible, Accumulate
      if (visibility > 0.0)
      {
        float3 materialResponse = bsdf.eval(light.direction, worldNormal);
        float3 irradiance = light.color * light.intensity;
        color += materialResponse * irradiance * visibility;
      }
    }
  }
  return color;
}
#endif
