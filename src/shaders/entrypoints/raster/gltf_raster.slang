#include "gltf_shared.h.slang"

struct VSin {
  float3 position : POSITION;
  float3 normal : NORMAl;
};

/**********************************************************/
[shader("vertex")] FragmentInput vertexMain(VSin input, uint vertexIndex
                                            : SV_VertexID)
/**********************************************************/
{
  int instanceIndex = pushConst.instanceIndex;

  SceneInfo *sceneInfo = pushConst.sceneInfoAddress.get();
  SceneResources *sceneResources =
      pushConst.resourcesAddress.get();

  Instance instance = sceneResources->instances[instanceIndex];
  MeshPrimitive meshIo = sceneResources->meshes[instance.meshIndex];
  Material material = sceneResources->materials[instance.materialIndex];

  float3 posMesh = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions,
                                        vertexIndex);
  float3 normal =
      getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals, vertexIndex);
  float2 texCoord = getAttribute<float2>(meshIo.buffer,
                                         meshIo.triMesh.texCoords, vertexIndex);

  float4 pos = mul(float4(posMesh, 1.0), instance.transform);

  FragmentInput output;
  output.debugColor = colorFromIndex(vertexIndex);
  output.instanceIndex = instanceIndex;
  output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(mul(normal, float3x3(pushConst.normalMatrix)));
  output.worldTexCoord = texCoord;

  return output;
}
