#include "gltf_shared.h.slang"

[shader("vertex")] FragmentInput vertexMain(uint logicalVertexID
                                            : SV_VertexID, uint drawSlot
                                            : SV_DrawIndex) {
  uint32_t *instanceMap = (uint32_t *)pushConst.instanceMapAddress;
  uint instanceID = instanceMap[drawSlot]; // true scene instance index
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  SceneResources *sceneResources = pushConst.resourcesAddress;

  Instance instance = sceneResources.instances[instanceID];
  MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];

  // ==========================================================
  // 1. PROGRAMMABLE INDEX PULLING
  // ==========================================================
  uint actualVertexIndex = getIndex(meshIo.buffer, meshIo.triMesh.indices,
                                    logicalVertexID, meshIo.indexType);
  // ==========================================================
  // 2. FETCH VERTEX PAYLOAD
  // ==========================================================
  float3 posMesh = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions,
                                        actualVertexIndex);
  float3 normal = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals,
                                       actualVertexIndex);
  float2 texCoord = getAttribute<float2>(
      meshIo.buffer, meshIo.triMesh.texCoords, actualVertexIndex);

  // ==========================================================
  // 3. TRANSFORMATIONS
  // ==========================================================
  float4 pos = mul(float4(posMesh, 1.0), instance.transform);

  float3x3 normalMatrix = transpose(inverse3x3((float3x3)instance.transform));

  FragmentInput output;
  output.debugColor = colorFromIndex(instanceID);
  output.instanceIndex = instanceID;
  output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(mul(normal, normalMatrix));
  output.worldTexCoord = texCoord;

  return output;
}
