// gltf_fragment.slang

#include "gltf_shared.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "include/light/lights.h.slang"
#include "shared/structs.h"

[shader("pixel")] PSout fragmentMain(FragmentInput stage) {
  SceneInfo sceneInfo;
  Instance  instance;
  Material  material;
  fetchFragmentResources(stage.instanceIndex, sceneInfo, instance, material);

  PSout output;
  float3 finalColor = float3(0);

  // Switch based on the hit_group (MaterialType)
  switch ((MaterialType)instance.hit_group) {
  case MaterialType::eDiffuse: {
    finalColor = stage.debugColor.rgb;
    break;
  }

  case MaterialType::eNormals: {
    finalColor = (stage.worldNormal.rgb + 1.0f) * 0.5f;
    break;
  }

  case MaterialType::eGltfPbr:
  default: {
    // Standard PBR path
    LightSample light = sampleLight(&sceneInfo, stage.worldPos);

    float3 V = normalize(sceneInfo.cameraPosition - stage.worldPos);
    float3 N = normalize(stage.worldNormal);
    float3 L = normalize(light.direction);

    // Get base color from material or texture
    float3 albedo = material.baseColorFactor.xyz;
    if (material.baseColorTextureIndex > 0) {
      albedo *= textures[material.baseColorTextureIndex]
                    .Sample(stage.worldTexCoord)
                    .xyz;
    }

    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;

    // PBR Calculation
    float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
    color *= light.color * light.intensity;

    // Ambient / Sky
    float3 ambient = sceneInfo.backgroundColor;
    if (sceneInfo.useSky == 1) {
      float3 skyUpDir = float3(0, 1, 0);
      float3 groundColor = sceneInfo.skySimpleParam.groundColor;
      float3 skyColor = sceneInfo.skySimpleParam.skyColor;
      ambient = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
    }

    finalColor = color + (ambient * albedo * 0.025);
    break;
  }
  }

  output.color = float4(clamp(finalColor, float3(0.0), float3(1.0)), 1.0);
  return output;
}
