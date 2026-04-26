// gltf_fragment.slang

#include "gltf_shared.h.slang"
#include "shared/structs.h"

[shader("fragment")] PSout fragmentMain(FragmentInput stage) {
  SceneInfo *sceneInfo = (SceneInfo *)pushConst.sceneInfoAddress;
  SceneResources *sceneResources = (SceneResources*)pushConst.resourcesAddress;
  Instance instance = ((Instance *)(sceneResources->instances))[stage.instanceIndex];

  PSout output;
  // TODO: call computeFragmentColor(sceneInfo, instance, material, stage) once
  // Metal bindless texture support is implemented.  The global `textures[]`
  // array is not yet bound in the Metal raster path, so any PBR/texture look-up
  // inside computeFragmentColor would read invalid memory on Metal.  Until
  // then, output the per-vertex debug colour (safe, texture-free).
  float3 finalColor = stage.debugColor.rgb;
  output.color = float4(clamp(finalColor, float3(0.0), float3(1.0)), 1.0);
  return output;
}
