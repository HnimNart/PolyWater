#include "gltf_shared.h.slang"

// 1. Declare the payload as groupshared memory globally
groupshared MeshTaskPayload sharedPayload;

[shader("task")][numthreads(TASK_WORKGROUP_SIZE, 1, 1)] void
taskMain(uint threadId : SV_GroupIndex,
         uint groupId : SV_GroupID) {
  int instanceIndex = pushConst.instanceIndex;
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  SceneResources *sceneResources = pushConst.resourcesAddress;

  Instance instance = sceneResources.instances[instanceIndex];
  MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];

  uint globalMeshletIndex = (groupId * TASK_WORKGROUP_SIZE) + threadId;

  bool accept = false;

  if (globalMeshletIndex < meshIo.meshlet.meshlets.count) {
    GPUMeshlet meshlet =
        getMeshlet(meshIo.buffer, meshIo.meshlet.meshlets, globalMeshletIndex);

    accept = frustumCullSphere(meshlet.center, meshlet.radius,
                               instance.transform, sceneInfo.frustumPlanes);
  }

  // Subgroup compaction
  uint numSurviving = WaveActiveCountBits(accept);
  uint emitIndex = WavePrefixCountBits(accept);

  // Write surviving global meshlet indices to the groupshared payload
  if (accept) {
    sharedPayload.meshletIndices[emitIndex] = globalMeshletIndex;
  }

  // Synchronize the workgroup!
  GroupMemoryBarrierWithGroupSync();

  // Dispatch the Mesh Shader
  if (threadId == 0) {
    DispatchMesh(numSurviving, 1u, 1u, sharedPayload);
  }
}
