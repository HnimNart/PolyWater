#ifndef VERTEX_FETCH_H_SLANG
#define VERTEX_FETCH_H_SLANG 1

/**********************************************************/
__generic<T : IFloat> T getAttribute(DevicePtr<uint8_t> dataBufferAddress,
                                     BufferView bufferView,
                                     uint attributeIndex)
/**********************************************************/
{
  if (bufferView.count > 0) {
    // glTF spec: if byteStride is 0, data is tightly packed
    uint actualStride =
        bufferView.byteStride == 0 ? sizeof(T) : bufferView.byteStride;

    Ptr<T> ptr = (Ptr<T>)dataBufferAddress.at(bufferView.offset +
                                              attributeIndex * actualStride);
    return ptr[0];
  }

  return T(1);
}

/**********************************************************/
__generic<T> int3 getTriangleIndices(DevicePtr<uint8_t> dataBufferAddress,
                                     const TriangleMesh mesh, int primitiveID,
                                     int indexType)
/**********************************************************/
{
  // 0 == VK_INDEX_TYPE_UINT16, 1 == VK_INDEX_TYPE_UINT32
  if (indexType == 0) {
    Ptr<int16_t3> indices = dataBufferAddress.at<int16_t3>(mesh.indices.offset);
    return indices[primitiveID];
  } else if (indexType == 1) {
    Ptr<int3> indices = dataBufferAddress.at<int3>(mesh.indices.offset);
    return indices[primitiveID];
  }

  return int3(-1);
}

/**********************************************************/
__generic<T : IFloat>
    T getTriangleAttribute(DevicePtr<uint8_t> dataBufferAddress,
                           BufferView bufferView, uint3 attributeIndex,
                           float3 barycentrics)
/**********************************************************/
{
  T attr0 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.x);
  T attr1 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.y);
  T attr2 = getAttribute<T>(dataBufferAddress, bufferView, attributeIndex.z);

  return T(barycentrics.x) * attr0 + T(barycentrics.y) * attr1 +
         T(barycentrics.z) * attr2;
}

/**********************************************************/
GPUMeshlet getMeshlet(DevicePtr<uint8_t> dataBufferAddress,
                      BufferView bufferView, uint meshletIndex)
/**********************************************************/
{
  Ptr<GPUMeshlet> ptr = (Ptr<GPUMeshlet>)dataBufferAddress.at(
      bufferView.offset + meshletIndex * bufferView.byteStride);
  return ptr[0];
}

/**********************************************************/
uint getMeshletVertex(DevicePtr<uint8_t> dataBufferAddress,
                      BufferView bufferView, uint elementIndex)
/**********************************************************/
{
  Ptr<uint> ptr = dataBufferAddress.at<uint>(
      bufferView.offset + elementIndex * bufferView.byteStride);
  return *ptr;
}

/**********************************************************/
uint8_t getMeshletTriangle(DevicePtr<uint8_t> dataBufferAddress,
                           BufferView bufferView, uint elementIndex)
/**********************************************************/
{
  Ptr<uint8_t> ptr = dataBufferAddress.at(bufferView.offset + elementIndex);
  return ptr[0];
}

struct HitState {
  float3 pos;
  float3 normal;
  float2 uv;
  float3 worldPos;
  float3 worldNormal;
  float3 viewDir;

  Material material;
};

/**********************************************************/
HitState getHitState(BuiltInTriangleIntersectionAttributes attr)
/**********************************************************/
{
  HitState s;

  float3 barycentrics = float3(1.0 - attr.barycentrics.x - attr.barycentrics.y,
                               attr.barycentrics.x, attr.barycentrics.y);

  uint instanceID = InstanceIndex();
  uint triID = PrimitiveIndex();

  Ptr<SceneResources> sceneResources = pushConst.resourcesAddress.get();
  Instance instance = sceneResources->instances[instanceID];
  MeshPrimitive mesh = sceneResources->meshes[instance.meshIndex];
  s.material = sceneResources->materials[instance.materialIndex];

  int3 indices = getTriangleIndices<int3>(mesh.buffer, mesh.triMesh, triID,
                                          mesh.indexType);

  s.pos = getTriangleAttribute<float3>(mesh.buffer, mesh.triMesh.positions,
                                       indices, barycentrics);
  s.normal = getTriangleAttribute<float3>(mesh.buffer, mesh.triMesh.normals,
                                          indices, barycentrics);
  s.uv = getTriangleAttribute<float2>(mesh.buffer, mesh.triMesh.texCoords,
                                      indices, barycentrics);

  s.worldPos = float3(mul(float4(s.pos, 1.0), ObjectToWorld4x3()));
  s.worldNormal = normalize(mul(WorldToObject4x3(), s.normal).xyz);
  s.viewDir = -WorldRayDirection();

  return s;
}

#endif
