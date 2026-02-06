#include "shared.h.slang"

//-----------------------------------------------------------------------
// DIFFUSE CLOSEST HIT SHADER - Simple Lambertian Reflection
//-----------------------------------------------------------------------
[shader("closesthit")] void
rchit_diffuse(inout HitPayload payload,
              in BuiltInTriangleIntersectionAttributes attr)
{
  // 1. Geometry Data Extraction
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y,
                               attr.barycentrics.x, attr.barycentrics.y);

  uint instanceID = InstanceIndex();
  uint triID = PrimitiveIndex();

  GltfSceneInfo sceneInfo = pushConst.sceneInfoAddress[0];
  GltfInstance instance = sceneInfo.instances[instanceID];
  GltfMesh mesh = sceneInfo.meshes[instance.meshIndex];
  GltfMetallicRoughness material = sceneInfo.materials[instance.materialIndex];

  // 2. Vertex Attribute Interpolation
  int3 indices = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 nrm = getTriangleAttribute<float3>(
      mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float2 uv = getTriangleAttribute<float2>(
      mesh.gltfBuffer, mesh.triMesh.texCoords, indices, barycentrics);
  float3 pos = getTriangleAttribute<float3>(
      mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);

  // 3. Coordinate Space Transformation
  float3 worldPos = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
  float3 worldNormal = normalize(
      mul(nrm, (float3x3) WorldToObject4x3()));  // Note: Normal transform is
                                                 // usually inverse transpose

  // 4. Material Base Color
  float3 albedo = material.baseColorFactor.xyz;
  if (material.baseColorTextureIndex > 0)
  {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(uv, 0).xyz;
  }

  // 5. Lighting Calculation
  GltfPunctual light = processLight(sceneInfo, worldPos);
  float3 N = normalize(worldNormal);
  float3 L = normalize(light.direction);

  // Lambertian Diffuse: (Albedo / PI) * dot(N, L)
  // We assume the PI is handled within your light intensity or final scaling
  float nDotL = max(0.0, dot(N, L));

  // 6. Shadow Testing
  float shadowFactor = testShadow(worldPos, N, light.direction, light);

  // 7. Final Composition
  // Simple diffuse doesn't use V (view direction) or metallic/roughness
  float3 finalColor =
      albedo * nDotL * light.color * light.intensity * shadowFactor;

  payload.color = finalColor;
}
