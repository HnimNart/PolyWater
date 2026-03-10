#include "gltf_shared.h.slang"

[[vk::push_constant]]
ConstantBuffer<PushConstant> pushConsts;

[shader("compute")][numthreads(64, 1, 1)] void
computeMain(uint3 threadId
            : SV_DispatchThreadID) {
  uint instanceIdx = threadId.x;
  if (instanceIdx >= pushConsts.totalSceneInstances)
    return;

  Instance inst = pushConsts.resourcesAddress->instances[instanceIdx];
  MeshPrimitive meshPrim = pushConsts.resourcesAddress->meshes[inst.meshIndex];
  if (isAABBInsideFrustum(pushConsts.sceneInfoAddress->frustumPlanes,
                               meshPrim.bbox.min, meshPrim.bbox.max,
                               inst.transform)) {

    DrawIndirectCommand *cmdBuffer =
        (DrawIndirectCommand *)pushConsts.indirectCommandsAddress;

    // 1. Increment this mesh's specific command
    uint localSlot;
    InterlockedAdd(cmdBuffer[inst.meshIndex].instanceCount, 1, localSlot);

    // 2. Read the static offset we pushed from CPU
    uint bucketOffset = cmdBuffer[inst.meshIndex].firstInstance;

    // 3. Store the instance ID in its specific mesh bucket
    uint *instanceMap = (uint *)pushConsts.instanceMapAddress;
    instanceMap[bucketOffset + localSlot] = instanceIdx;
  }
}
