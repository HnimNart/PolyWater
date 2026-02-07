#include "nvshaders/sky_functions.h.slang"
#include "shared.h.slang"

//-----------------------------------------------------------------------
// MISS SHADER - Called when ray doesn't hit any geometry (background/sky)
//-----------------------------------------------------------------------
[shader("miss")] void rmissMain(inout HitPayload payload)
{
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress;

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

  // Set depth to MISS_DEPTH to indicate this ray has finished (no more bounces)
  payload.depth = MISS_DEPTH;
}
