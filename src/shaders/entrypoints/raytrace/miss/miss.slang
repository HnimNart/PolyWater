#include "include/light/sky_functions.h.slang"
#include "include/light/envmap.h.slang"
#include "entrypoints/raytrace/ray_common.h.slang"


/**********************************************************/
[shader("miss")] void rmissMain(inout HitPayload payload)
/**********************************************************/
{
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress.get();

  if (payload.emit == 0)
  {
    payload.stop = true;
    return;
  }

  if (sceneInfo.useEnv == 1) {
      EnvmapSampler env;
      payload.color += env.evalRadiance(sceneInfo.envmapLight, WorldRayDirection(), textures) * payload.weight;
  }
  else if (sceneInfo.useSky == 1)
  {
    float3 skyColor =
        evalSimpleSky(sceneInfo.skySimpleParam, WorldRayDirection());
    payload.color += skyColor * payload.weight;
  }
  else
  {
    payload.color += sceneInfo.backgroundColor * payload.weight;
  }

  payload.stop = true;
}
