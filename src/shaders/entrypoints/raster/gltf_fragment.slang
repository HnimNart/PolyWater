// gltf_fragment.slang

#include "gltf_shared.h.slang"
#include "shared/structs.h"

[shader("pixel")] PSout fragmentMain(FragmentInput stage) {
  SceneInfo sceneInfo;
  Instance  instance;
  Material  material;
  fetchFragmentResources(stage.instanceIndex, sceneInfo, instance, material);

  PSout output;
  float3 finalColor = computeFragmentColor(sceneInfo, instance, material, stage);
  output.color = float4(clamp(finalColor, float3(0.0), float3(1.0)), 1.0);
  return output;
}
