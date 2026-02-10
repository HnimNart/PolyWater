#include "nvshaders/sky_functions.h.slang"
#include "entrypoints/raytrace/ray_common.h.slang"

//-----------------------------------------------------------------------
// MISS SHADER - Called when ray doesn't hit any geometry (background/sky)
//-----------------------------------------------------------------------
[shader("miss")] void rmissMain(inout HitPayload payload)
{
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  if (payload.emit == 0)
  {
    payload.stop = true;
    return;
  }

  // Check if sky system is enabled
  if (sceneInfo.useSky == 1)
  {
    // Evaluate procedural sky color based on ray direction
    // This creates realistic sky gradients based on sun position and
    // atmospheric scattering
    float3 skyColor =
        evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
    payload.color += skyColor * payload.weight;
  }
  else
  {
    // Use simple solid background color when sky system is disabled
    payload.color += sceneInfo.backgroundColor * payload.weight;
  }

  payload.stop = true;
}
