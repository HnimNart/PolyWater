#include "gltf_shared.h.slang"

// 1. Declare the payload as groupshared memory globally
groupshared MeshTaskPayload sharedPayload;
groupshared uint groupSurvivorCount; // Move this here!



/**********************************************************/
bool hiZCullSphere(float3 localCenter, float localRadius, float4x4 transform, SceneInfo* sceneInfo) 
/**********************************************************/
{
    // 1. World Transform (Using the exact working math from Frustum pass)
    float4 worldCenter = float4(mul(float4(localCenter, 1.0), transform).xyz, 1.0);
    
    // Extract Max Scale (Matches Frustum pass)
    float scaleX = length(float3(transform[0][0], transform[1][0], transform[2][0]));
    float scaleY = length(float3(transform[0][1], transform[1][1], transform[2][1]));
    float scaleZ = length(float3(transform[0][2], transform[1][2], transform[2][2]));
    float maxScale = max(scaleX, max(scaleY, scaleZ));
    float worldRadius = localRadius * maxScale;

    // 2. View Transform
    // Since transform used mul(vec, mat), viewMatrix almost certainly needs it too
    float4 viewCenter = mul(worldCenter, sceneInfo->viewMatrix);

    // 3. Find the Front of the Sphere (Prevents self-occlusion from previous frame)
    float nearestZ = viewCenter.z + worldRadius;

    // Safety: If sphere touches the near plane, don't cull
    if (nearestZ >= -sceneInfo->nearZ) return false; 

    // 4. Aspect-Ratio Correct AABB
    float absZ = abs(viewCenter.z);
    
    // CRITICAL: We MUST use abs() here because Vulkan's projMatrix[1][1] is negative!
    float screenRadiusX = (worldRadius * abs(sceneInfo->projMatrix[0][0])) / absZ;
    float screenRadiusY = (worldRadius * abs(sceneInfo->projMatrix[1][1])) / absZ;

    // Project using mul(vec, mat)
    float4 clipCenter = mul(viewCenter, sceneInfo->projMatrix);
    float2 ndc = clipCenter.xy / clipCenter.w;
    
    // Because we used abs() on the radius, uvMin will properly be smaller than uvMax
    float2 uvMin = saturate((ndc - float2(screenRadiusX, screenRadiusY)) * 0.5 + 0.5);
    float2 uvMax = saturate((ndc + float2(screenRadiusX, screenRadiusY)) * 0.5 + 0.5);

    // 5. Mip Selection
    float2 sizeInPixels = (uvMax - uvMin) * pushConst->screenResolution;
    float maxDim = max(sizeInPixels.x, sizeInPixels.y);
    float mip = max(0.0, floor(log2(maxDim)));

    // 6. Sample Hi-Z
    float d1 = hiZTexture.SampleLevel(hiZSampler, float2(uvMin.x, uvMin.y), mip).r;
    float d2 = hiZTexture.SampleLevel(hiZSampler, float2(uvMax.x, uvMin.y), mip).r;
    float d3 = hiZTexture.SampleLevel(hiZSampler, float2(uvMin.x, uvMax.y), mip).r;
    float d4 = hiZTexture.SampleLevel(hiZSampler, float2(uvMax.x, uvMax.y), mip).r;
    
    float maxHiZDepth = max(max(d1, d2), max(d3, d4));

    // 7. Project Front Depth
    float4 nearestClip = mul(float4(0, 0, nearestZ, 1.0), sceneInfo->projMatrix);
    float sphereDepth = nearestClip.z / nearestClip.w;

    // 8. Cull
    // If the front of our sphere is behind the furthest background point, cull!
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

  // Calculate the absolute global thread ID across the entire scene
  uint globalThreadId = (groupId * TASK_WORKGROUP_SIZE) + threadId;

  bool accept = false;
  uint instanceIdx = 0;
  uint meshletIdx = 0;

  // Ensure we don't read past the end of our global array
  if (globalThreadId < pushConst.totalSceneMeshlets) {
    
    GlobalMeshletRef ref = pushConst.globalMeshletRefsAddress[globalThreadId];
    instanceIdx = ref.instanceIndex;
    meshletIdx = ref.localMeshletIndex;

    // Fetch instance and mesh data using the dynamically looked-up instance
    Instance instance = sceneResources.instances[instanceIdx];
    MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];

    // Fetch the specific meshlet for this thread
    GPUMeshlet meshlet = getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, meshletIdx);

    // Cull against the frustum
    accept = frustumCullSphere(meshlet.center, meshlet.radius,
                               instance.transform, sceneInfo.frustumPlanes);
    // Cull against the Hi-Z Buffer (Only if it survived frustum culling)
    if (accept) {
        accept = !hiZCullSphere(meshlet.center, meshlet.radius, 
                               instance.transform, sceneInfo);
    }
  }

  // 2. Initialize counter
  if (threadId == 0) groupSurvivorCount = 0;
  GroupMemoryBarrierWithGroupSync();

  uint numSurviving = 0;
  uint emitIndex = 0;


  // 3. Target-specific compaction
  __target_switch {
    case metal: {
        // Fallback: Atomic compaction for Metal
        if (accept) {
            InterlockedAdd(groupSurvivorCount, 1, emitIndex);
        }
        GroupMemoryBarrierWithGroupSync();
        numSurviving = groupSurvivorCount;
    }
    default: {
        // High-performance Subgroup compaction for Vulkan/SPIR-V
        numSurviving = WaveActiveCountBits(accept);
        emitIndex = WavePrefixCountBits(accept);
    }
  }

  // Write surviving global meshlet and instance indices to the payload
  if (accept) {
    sharedPayload.meshletIndices[emitIndex] = meshletIdx;
    sharedPayload.instanceIndices[emitIndex] = instanceIdx; 
  }

  // Synchronize the workgroup!
  GroupMemoryBarrierWithGroupSync();

  // Dispatch the Mesh Shader with the exact number of survivors
  if (threadId == 0) {
    DispatchMesh(numSurviving, 1u, 1u, sharedPayload);
  }
}

