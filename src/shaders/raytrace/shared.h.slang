#ifndef SHARED_H
#define SHARED_H 1

#include "include/lights.h.slang"
#include "include/pbr.h.slang"
#include "nvshaders/constants.h.slang"
#include "shaderio.h"

#define MISS_DEPTH 1000

// clang-format off
// Push constants containing scene information, camera data, and material overrides
[[vk::push_constant]]                           ConstantBuffer<PushConstant> pushConst;
// Texture array for material textures (albedo, normal maps, etc.)
[[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
// Top-level acceleration structure containing the scene geometry hierarchy
[[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
// Output image where the final rendered result will be stored
[[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// clang-format on

// Ray payload structure - carries data through the ray tracing pipeline
struct HitPayload
{
  float3 color;  // Accumulated color along the ray path
  float weight;  // Weight/importance of this ray (for importance sampling)
  int depth;     // Current recursion depth (for limiting bounces)
};

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

//-----------------------------------------------------------------------
// SHADOW TESTING - Determines if a point is in shadow from a light source
//-----------------------------------------------------------------------
// Tests visibility between a surface point and a light source
// Returns 1.0 if fully lit, 0.0 if fully shadowed
float testShadow(float3 worldPos, float3 worldNormal, float3 lightDirection,
                 GltfPunctual light)
{
  // Create shadow ray from surface point towards light source
  RayDesc shadowRay;
  // Offset origin slightly along normal to avoid self-intersection artifacts
  shadowRay.Origin = worldPos + worldNormal * EPSILON;
  shadowRay.Direction = normalize(lightDirection);
  shadowRay.TMin = EPSILON;  // Minimum distance to avoid self-intersection
  // For directional lights, trace to infinity; for point/spot lights, trace to
  // light position
  shadowRay.TMax = light.type == GltfLightType::eDirectional
                       ? INFINITE
                       : length(lightDirection);

  // Create a minimal payload for shadow ray (we only need to know if it hits
  // something)
  HitPayload shadowPayload;
  shadowPayload.depth = 0;  // Will be set to MISS_DEPTH if no hit occurs

  // Trace the shadow ray with optimized flags for performance:
  // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: Stop at first intersection (we
  // don't need closest) RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: Skip expensive
  // shading calculations
  TraceRay(topLevelAS,
           RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
               RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
           0xff, 0, 0, 0, shadowRay, shadowPayload);

  // If the shadow ray hit something (depth != MISS_DEPTH), the light is
  // occluded
  return shadowPayload.depth != MISS_DEPTH ? 0.0 : 1.0;
}

#endif
