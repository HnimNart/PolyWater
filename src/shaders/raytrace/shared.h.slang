#ifndef SHARED_H
#define SHARED_H 1

#define RAY_TRACE 1

#include "include/lights.h.slang"
#include "include/math.h.slang"
#include "include/pbr.h.slang"
#include "include/rnd.h.slang"
#include "include/utils.h.slang"
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
// Output image where the final rendered result will be stored
[[vk::binding(BindingPoints::eAccumImage, 1)]]    RWTexture2D<float4> accumImage;
// clang-format on

// Ray payload structure - carries data through the ray tracing pipeline
struct HitPayload
{
  float3 color;   // Accumulated color along the ray path
  float3 weight;  // Weight/importance of this ray (for importance sampling)
  int depth;      // Current recursion depth (for limiting bounces)
  int seed;
  int emit;  // Should we include emitting surfaces in the contribution
  float3 nextRayOrigin;  // Where the bounce starts
  float3 nextRayDir;     // Where the bounce goes
  bool stop;             // "Did we hit the sky or a black hole?"
};

//-----------------------------------------------------------------------
// SHADOW TESTING - Uses existing HitPayload
// Returns 1.0 if visible (lit), 0.0 if occluded (shadow)
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 dirToLight,
                 float distance)
{
  RayDesc shadowRay;
  shadowRay.Origin = worldPos;
  shadowRay.Direction = dirToLight;
  shadowRay.TMin = EPSILON;
  shadowRay.TMax = distance - EPSILON;  // Don't hit the light itself

  // Use the FAT payload (HitPayload)
  HitPayload shadowPayload;

  // LOGIC: Assume we are BLOCKED (Shadowed) by default.
  // If the ray hits nothing, the Miss Shader will flip this to false.
  shadowPayload.stop = false;

  // Reset other fields just in case (good practice, though not strictly needed
  // for shadow)
  shadowPayload.depth = 0;
  shadowPayload.color = float3(0);

  // Trace!
  // RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: We don't care about the object's color.
  // RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: Stop as soon as we hit anything
  // opaque.
  TraceRay(
      topLevelAS,
      RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
          RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
      0xff, 0, 0,
      0,  // <--- IMPORTANT: Ensure SBT Miss Index 1 is your shadow miss shader
      shadowRay, shadowPayload);

  return shadowPayload.stop ? 1.0 : 0.0;
}

// -----------------------------------------------------------------------
// Helper: Builds an Orthonormal Basis (Tangent Space) from a Normal
// Duff et al. method - robust and singularity-free
// -----------------------------------------------------------------------
void createCoordinateSystem(in float3 N, out float3 T, out float3 B)
{
  float sign = N.z >= 0.0f ? 1.0f : -1.0f;
  float a = -1.0f / (sign + N.z);
  float b = N.x * N.y * a;

  T = float3(1.0f + sign * N.x * N.x * a, sign * b, -sign * N.x);
  B = float3(b, sign + N.y * N.y * a, -N.y);
}

// -----------------------------------------------------------------------
// Helper: Generates a Cosine-Weighted random direction
// Returns: A normalized World Space direction centered around 'normal'
// -----------------------------------------------------------------------
float3 sampleCosineWeightedHemisphere(float3 normal, inout uint seed)
{
  // A. Generate Random Numbers
  float r1 = rnd(seed);
  float r2 = rnd(seed);

  // B. Calculate Local Cosine Sample
  // Optimization: We can avoid expensive acos() and sin() calls
  // because sin^2 + cos^2 = 1.
  float phi = 2.0 * 3.14159265 * r1;
  float cosTheta = sqrt(r2);        // z component
  float sinTheta = sqrt(1.0 - r2);  // radius on xy plane

  float3 localDir;
  localDir.x = sinTheta * cos(phi);
  localDir.y = sinTheta * sin(phi);
  localDir.z = cosTheta;

  // C. Transform Local -> World Space
  float3 tangent, bitangent;
  createCoordinateSystem(normal, tangent, bitangent);

  return localDir.x * tangent + localDir.y * bitangent + localDir.z * normal;
}

#endif
