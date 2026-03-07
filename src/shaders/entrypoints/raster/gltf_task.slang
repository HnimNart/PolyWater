#include "gltf_shared.h.slang"

// 1. Declare the payload as groupshared memory globally
groupshared MeshTaskPayload sharedPayload;

[shader("task")]
[numthreads(TASK_WORKGROUP_SIZE, 1, 1)] 
void taskMain(uint threadId : SV_GroupIndex, uint groupId : SV_GroupID) 
{
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  SceneResources *sceneResources = pushConst.resourcesAddress;

  // Calculate the absolute global thread ID across the entire scene
  uint globalThreadId = (groupId * TASK_WORKGROUP_SIZE) + threadId;

  bool accept = false;
  uint instanceIdx = 0;
  uint meshletIdx = 0;

  // Ensure we don't read past the end of our global array
  if (globalThreadId < pushConst.totalSceneMeshlets) {
    
    // <-- READ DIRECTLY FROM THE BDA POINTER!
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
  }

  // Subgroup compaction (Find out how many survived in this group of 32)
  uint numSurviving = WaveActiveCountBits(accept);
  uint emitIndex = WavePrefixCountBits(accept);

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

