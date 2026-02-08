#ifndef UTILS_H
#define UTILS_H 1

// Generic function to retrieve vertex attributes from GLTF buffer data
// T: Type of attribute (float, float2, float3, etc.)
// dataBufferAddress: Base address of the GLTF buffer
// bufferView: Buffer view containing offset, stride, and count information
// attributeIndex: Index of the vertex attribute to retrieve
__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress,
                                     BufferView bufferView, uint attributeIndex)
{
  if (bufferView.count > 0)
  {
    // Calculate pointer to the specific attribute using offset and stride
    T* ptr = (T*) (dataBufferAddress + bufferView.offset +
                   attributeIndex * bufferView.byteStride);
    return ptr[0];
  }

  return T(1);  // Error case - return default value
}

// Retrieve triangle vertex indices from the mesh index buffer
// Supports both 16-bit and 32-bit index formats as per GLTF specification
int3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh,
                        int primitiveID)
{
  if (mesh.indices.byteStride == sizeof(int16_t))
  {
    // 16-bit indices (uint16_t) - more memory efficient for smaller meshes
    int16_t3* indices = (int16_t3*) (dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if (mesh.indices.byteStride == sizeof(int32_t))
  {
    // 32-bit indices (uint32_t) - supports larger meshes with more vertices
    int3* indices = (int3*) (dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return int3(-1);  // Error case - invalid index format
}

// Interpolate vertex attributes across a triangle using barycentric coordinates
// This performs smooth interpolation of attributes like position, normal, UV
// coordinates T: Type of attribute to interpolate (float, float2, float3, etc.)
// attributeIndex: Indices of the three triangle vertices
// barycentrics: Barycentric coordinates (weights for each vertex)
__generic<T : IFloat> T getTriangleAttribute(uint8_t* dataBufferAddress,
                                             BufferView bufferView,
                                             uint3 attributeIndex,
                                             float3 barycentrics)
{
  // Get the attribute value for each of the three triangle vertices
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);

  // Interpolate using barycentric coordinates (weights sum to 1.0)
  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 +
         T(barycentrics.z) * attr2;
}

struct HitState
{
  float3 pos;
  float3 normal;
  float2 uv;
  float3 worldPos;
  float3 worldNormal;
  float3 viewDir;  // V

  // Material info usually goes hand-in-hand with geometry
  Material material;
};

// Helper to unpack everything
HitState getHitState(BuiltInTriangleIntersectionAttributes attr)
{
  HitState s;

  // 1. Barycentrics
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y,
                               attr.barycentrics.x, attr.barycentrics.y);

  // 2. IDs
  uint instanceID = InstanceIndex();
  uint triID = PrimitiveIndex();

  // 3. Fetch Resources
  // Note: We access pushConst global directly since this is a helper included
  // in the shader
  SceneResources sceneResources = pushConst.resourcesAddress[0];
  Instance instance = sceneResources.instances[instanceID];
  MeshPrimitive mesh = sceneResources.meshes[instance.meshIndex];

  s.material = sceneResources.materials[instance.materialIndex];

  // 4. Interpolate Attributes
  int3 indices = getTriangleIndices(mesh.buffer, mesh.triMesh, triID);

  s.pos = getTriangleAttribute<float3>(mesh.buffer, mesh.triMesh.positions,
                                       indices, barycentrics);
  s.normal = getTriangleAttribute<float3>(mesh.buffer, mesh.triMesh.normals,
                                          indices, barycentrics);
  s.uv = getTriangleAttribute<float2>(mesh.buffer, mesh.triMesh.texCoords,
                                      indices, barycentrics);

  // 5. Transform to World Space
  s.worldPos = float3(mul(float4(s.pos, 1.0), ObjectToWorld4x3()));
  s.worldNormal = normalize(mul(WorldToObject4x3(), s.normal).xyz);
  s.viewDir = -WorldRayDirection();

  return s;
}

#endif
