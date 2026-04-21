#ifndef VERTEX_FETCH_H_SLANG
#define VERTEX_FETCH_H_SLANG 1

// Generic function to retrieve vertex attributes from GLTF buffer data
__generic<T : IFloat> T getAttribute(uint8_t* dataBufferAddress,
                                     BufferView bufferView, uint attributeIndex)
{
  if (bufferView.count > 0)
  {
    // glTF spec: If byteStride is 0, the data is tightly packed.
    // So the actual stride is the size of the data type (T).
    uint actualStride = bufferView.byteStride == 0 ? sizeof(T) : bufferView.byteStride;

    // Calculate pointer to the specific attribute using offset and actual stride
    T* ptr = (T*) (dataBufferAddress + bufferView.offset + attributeIndex * actualStride);
    return ptr[0];
  }

  return T(1);  // Error case - return default value
}

// Retrieve triangle vertex indices from the mesh index buffer
int3 getTriangleIndices(uint8_t* dataBufferAddress, const TriangleMesh mesh,
                        int primitiveID, int indexType)
{
  // 0 == VK_INDEX_TYPE_UINT16, 1 == VK_INDEX_TYPE_UINT32
  if (indexType == 0)
  {
    // 16-bit indices
    int16_t3* indices = (int16_t3*) (dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }
  else if (indexType == 1)
  {
    int3* indices = (int3*) (dataBufferAddress + mesh.indices.offset);
    return indices[primitiveID];
  }

  return int3(-1);  // Error case - invalid index format
}

// Interpolate vertex attributes across a triangle using barycentric coordinates
__generic<T : IFloat> T getTriangleAttribute(uint8_t* dataBufferAddress,
                                             BufferView bufferView,
                                             uint3 attributeIndex,
                                             float3 barycentrics)
{
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);

  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 +
         T(barycentrics.z) * attr2;
}

// Helper to fetch the GPUMeshlet struct from the raw buffer
GPUMeshlet getMeshlet(uint8_t* dataBufferAddress, BufferView bufferView, uint meshletIndex)
{
    GPUMeshlet* ptr = (GPUMeshlet*)(dataBufferAddress + bufferView.offset + meshletIndex * bufferView.byteStride);
    return ptr[0];
}

// Helper to fetch the 32-bit global vertex index
uint getMeshletVertex(uint8_t* dataBufferAddress, BufferView bufferView, uint elementIndex)
{
    uint* ptr = (uint*)(dataBufferAddress + bufferView.offset + elementIndex * bufferView.byteStride);
    return ptr[0];
}

// Helper to fetch the tightly packed 8-bit local triangle index
uint8_t getMeshletTriangle(uint8_t* dataBufferAddress, BufferView bufferView, uint elementIndex)
{
    uint8_t* ptr = (uint8_t*)(dataBufferAddress + bufferView.offset + elementIndex);
    return ptr[0];
}

struct HitState
{
  float3 pos;
  float3 normal;
  float2 uv;
  float3 worldPos;
  float3 worldNormal;
  float3 viewDir;

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
  SceneResources sceneResources = pushConst.resourcesAddress[0];
  Instance instance = sceneResources.instances[instanceID];
  MeshPrimitive mesh = sceneResources.meshes[instance.meshIndex];

  s.material = sceneResources.materials[instance.materialIndex];

  // 4. Interpolate Attributes
  // We pass mesh.indexType to safely handle 16-bit vs 32-bit indices
  int3 indices = getTriangleIndices(mesh.buffer, mesh.triMesh, triID, mesh.indexType);

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


// ---------------------------------------------------------------------------
// Metal-safe vertex attribute readers
// ---------------------------------------------------------------------------
// Metal's shader compiler forbids as_type reinterpretation of a device pointer
// to an integer type (e.g. "as_type<ulong>(device uint8_t*)"), which is what
// Slang generates for a C-style pointer cast such as "(T*)(uint8_t_ptr)".
// The helpers below avoid that entirely: they index into the byte buffer
// directly (legal uint8_t array access) and reconstruct each float via scalar
// as_type<float>(uint), which Metal does permit.
// These are used by gltf_raster.slang through a __target_switch block so that
// non-Metal targets continue to use the generic getAttribute function.

// Read one float from a raw byte buffer at the given byte offset.
float _readFloatFromBytes(uint8_t* buf, uint byteOffset)
{
    uint word = uint(buf[byteOffset    ])
              | (uint(buf[byteOffset + 1]) <<  8)
              | (uint(buf[byteOffset + 2]) << 16)
              | (uint(buf[byteOffset + 3]) << 24);
    return as_type<float>(word);
}

// Read a float3 attribute from a raw byte buffer using a BufferView descriptor.
float3 _getFloat3FromBytes(uint8_t* buf, BufferView view, uint index)
{
    if (view.count == 0) return float3(1);
    uint stride = view.byteStride == 0 ? 12u : view.byteStride;
    uint base   = view.offset + index * stride;
    return float3(_readFloatFromBytes(buf, base),
                  _readFloatFromBytes(buf, base + 4),
                  _readFloatFromBytes(buf, base + 8));
}

// Read a float2 attribute from a raw byte buffer using a BufferView descriptor.
float2 _getFloat2FromBytes(uint8_t* buf, BufferView view, uint index)
{
    if (view.count == 0) return float2(1);
    uint stride = view.byteStride == 0 ? 8u : view.byteStride;
    uint base   = view.offset + index * stride;
    return float2(_readFloatFromBytes(buf, base),
                  _readFloatFromBytes(buf, base + 4));
}

#endif

