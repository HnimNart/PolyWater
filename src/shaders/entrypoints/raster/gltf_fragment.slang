#include "gltf_shared.h.slang"
#include "include/bsdf/pbr.h.slang"
#include "include/light/lights.h.slang"
#include "shared/structs.h"

static const float kShadowBias     = 0.005f;
static const float kShadowTexelSize = 1.0f / 2048.0f;

/**********************************************************/
float samplePCF(SceneInfo* sceneInfo, float3 worldPos)
/**********************************************************/
{
  if (sceneInfo->hasShadowMap == 0)
    return 1.0f;

  float4 lightClip = mul(float4(worldPos, 1.0f), sceneInfo->lightViewProjMatrix);
  float3 ndc = lightClip.xyz / lightClip.w;

  // Remap XY from [-1,1] to [0,1]; Vulkan Y is already top-down
  float2 uv = ndc.xy * 0.5f + 0.5f;

  // Discard fragments outside the light frustum
  if (any(uv < float2(0.0f)) || any(uv > float2(1.0f)))
    return 1.0f;

  float currentDepth = ndc.z - kShadowBias;

  // 3x3 PCF kernel using hardware comparison sampling
  float shadow = 0.0f;
  for (int x = -1; x <= 1; x++)
  {
    for (int y = -1; y <= 1; y++)
    {
      float2 offset = float2(x, y) * kShadowTexelSize;
      shadow += shadowMap.SampleCmpLevelZero(shadowSampler, uv + offset, currentDepth);
    }
  }
  return shadow / 9.0f;
}

/**********************************************************/
[shader("pixel")] PSout fragmentMain(FragmentInput stage)
/**********************************************************/
{
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress.get();
  SceneResources *sceneResources = pushConst.resourcesAddress.get();
  Instance instance = sceneResources->instances[stage.instanceIndex];
  Material material = sceneResources->materials[instance.materialIndex];

  PSout output;
  float3 finalColor = float3(0);

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
    LightSample light = sampleLight(sceneInfo, stage.worldPos);

    float3 V = normalize(sceneInfo->cameraPosition - stage.worldPos);
    float3 N = normalize(stage.worldNormal);
    float3 L = normalize(light.direction);

    float3 albedo = material.baseColorFactor.xyz;
    if (material.baseColorTextureIndex > 0) {
      albedo *= textures[material.baseColorTextureIndex]
                    .Sample(stage.worldTexCoord)
                    .xyz;
    }

    float metallic = material.metallicFactor;
    float roughness = material.roughnessFactor;

    float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
    color *= light.color * light.intensity;

    float shadowFactor = samplePCF(sceneInfo, stage.worldPos);
    color *= shadowFactor;

    float3 ambient = sceneInfo->backgroundColor;
    if (sceneInfo->useSky == 1) {
      float3 skyUpDir = float3(0, 1, 0);
      float3 groundColor = sceneInfo->skySimpleParam.groundColor;
      float3 skyColor = sceneInfo->skySimpleParam.skyColor;
      ambient = lerp(groundColor, skyColor, dot(N, skyUpDir) * 0.5 + 0.5);
    }

    finalColor = color + (ambient * albedo * 0.025);
    break;
  }
  }

  output.color = float4(clamp(finalColor, float3(0.0), float3(1.0)), 1.0);
  return output;
}
