#include "gltf_shared.h.slang"

groupshared MeshTaskPayload sharedPayload;
groupshared uint groupSurvivorCount;



/**********************************************************/
bool hiZCullSphere(float3 localCenter, float localRadius, float4x4 transform, SceneInfo* sceneInfo) 
/**********************************************************/
{
    float4 worldCenter = float4(mul(float4(localCenter, 1.0), transform).xyz, 1.0);
    
    float scaleX = length(float3(transform[0][0], transform[1][0], transform[2][0]));
    float scaleY = length(float3(transform[0][1], transform[1][1], transform[2][1]));
    float scaleZ = length(float3(transform[0][2], transform[1][2], transform[2][2]));
    float maxScale = max(scaleX, max(scaleY, scaleZ));
    float worldRadius = localRadius * maxScale;

    float4 viewCenter = mul(worldCenter, sceneInfo->viewMatrix);

    float nearestZ = viewCenter.z + worldRadius;

    if (nearestZ >= -sceneInfo->nearZ) return false; 

    float absZ = abs(viewCenter.z);
    
    float screenRadiusX = (worldRadius * abs(sceneInfo->projMatrix[0][0])) / absZ;
    float screenRadiusY = (worldRadius * abs(sceneInfo->projMatrix[1][1])) / absZ;

    float4 clipCenter = mul(viewCenter, sceneInfo->projMatrix);
    float2 ndc = clipCenter.xy / clipCenter.w;
    
    float2 uvMin = saturate((ndc - float2(screenRadiusX, screenRadiusY)) * 0.5 + 0.5);
    float2 uvMax = saturate((ndc + float2(screenRadiusX, screenRadiusY)) * 0.5 + 0.5);

    float2 sizeInPixels = (uvMax - uvMin) * pushConst->screenResolution;
    float maxDim = max(sizeInPixels.x, sizeInPixels.y);
    float mip = max(0.0, floor(log2(maxDim)));

    float d1 = hiZTexture.SampleLevel(hiZSampler, float2(uvMin.x, uvMin.y), mip).r;
    float d2 = hiZTexture.SampleLevel(hiZSampler, float2(uvMax.x, uvMin.y), mip).r;
    float d3 = hiZTexture.SampleLevel(hiZSampler, float2(uvMin.x, uvMax.y), mip).r;
    float d4 = hiZTexture.SampleLevel(hiZSampler, float2(uvMax.x, uvMax.y), mip).r;
    
    float maxHiZDepth = max(max(d1, d2), max(d3, d4));

    float4 nearestClip = mul(float4(0, 0, nearestZ, 1.0), sceneInfo->projMatrix);
    float sphereDepth = nearestClip.z / nearestClip.w;

    return sphereDepth > (maxHiZDepth + 0.00f); 
}



/**********************************************************/
[shader("task")]
[numthreads(TASK_WORKGROUP_SIZE, 1, 1)] 
void taskMain(uint threadId : SV_GroupIndex, uint groupId : SV_GroupID) 
/**********************************************************/
{
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress.get();
  SceneResources *sceneResources = pushConst.resourcesAddress.get();

  uint globalThreadId = (groupId * TASK_WORKGROUP_SIZE) + threadId;

  bool accept = false;
  uint instanceIdx = 0;
  uint meshletIdx = 0;

  if (globalThreadId < pushConst.totalSceneMeshlets) {
    
    GlobalMeshletRef ref = pushConst.globalMeshletRefsAddress[globalThreadId];
    instanceIdx = ref.instanceIndex;
    meshletIdx = ref.localMeshletIndex;

    Instance instance = sceneResources.instances[instanceIdx];
    MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];

    GPUMeshlet meshlet = getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, meshletIdx);

    accept = frustumCullSphere(meshlet.center, meshlet.radius,
                               instance.transform, sceneInfo.frustumPlanes);
    if (accept) {
        accept = !hiZCullSphere(meshlet.center, meshlet.radius, 
                               instance.transform, sceneInfo);
    }
  }

  if (threadId == 0) groupSurvivorCount = 0;
  GroupMemoryBarrierWithGroupSync();

  uint numSurviving = 0;
  uint emitIndex = 0;


  __target_switch {
    case metal: {
        if (accept) {
            InterlockedAdd(groupSurvivorCount, 1, emitIndex);
        }
        GroupMemoryBarrierWithGroupSync();
        numSurviving = groupSurvivorCount;
    }
    default: {
        numSurviving = WaveActiveCountBits(accept);
        emitIndex = WavePrefixCountBits(accept);
    }
  }

  if (accept) {
    sharedPayload.meshletIndices[emitIndex] = meshletIdx;
    sharedPayload.instanceIndices[emitIndex] = instanceIdx; 
  }

  GroupMemoryBarrierWithGroupSync();

  if (threadId == 0) {
    DispatchMesh(numSurviving, 1u, 1u, sharedPayload);
  }
}

