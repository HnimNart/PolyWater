// Shadow-map depth pass: renders scene geometry from the light's perspective.
// Only a vertex shader is used; there is no fragment shader (depth-only pass).

#include "shared/slang_types.h"
#include "shared/structs.h"
#include "include/geometry/vertex_fetch.h.slang"

// Push constants (same layout as the raster pass, only a subset is needed here)
[[vk::push_constant]] ConstantBuffer<PushConstant> pushConst;

struct ShadowVSOut
{
  float4 sv_position : SV_Position;
};

/**********************************************************/
[shader("vertex")] ShadowVSOut shadowVertexMain(uint vertexIndex : SV_VertexID)
/**********************************************************/
{
  int instanceIndex = pushConst.instanceIndex;

  SceneInfo*      sceneInfo      = pushConst.sceneInfoAddress.get();
  SceneResources* sceneResources = pushConst.resourcesAddress.get();

  Instance      instance = sceneResources->instances[instanceIndex];
  MeshPrimitive meshIo   = sceneResources->meshes[instance.meshIndex];

  float3 posMesh = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions, vertexIndex);

  float4 worldPos = mul(float4(posMesh, 1.0f), instance.transform);

  ShadowVSOut output;
  output.sv_position = mul(worldPos, sceneInfo->lightViewProjMatrix);
  return output;
}
