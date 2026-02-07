#ifndef SHARED_H
#define SHARED_H 1

#define RAY_TRACE 1

#include "include/lights.h.slang"
#include "include/math.h.slang"
#include "include/pbr.h.slang"
#include "include/rnd.h.slang"
#include "include/sampler.h.slang"
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

struct ShadowPayload
{
  bool isHit;
};

//-----------------------------------------------------------------------
// Returns 1.0 if visible (lit), 0.0 if occluded (shadow)
//-----------------------------------------------------------------------
float testShadow(float3 worldPos, float3 worldNormal, float3 dirToLight,
                 float distance)
{
  RayDesc shadowRay;
  // Push the origin slightly out along the normal to prevent self-intersection
  shadowRay.Origin = worldPos;
  shadowRay.Direction = dirToLight;
  shadowRay.TMin = EPSILON;
  shadowRay.TMax = distance - EPSILON;

  // 1. Initialize Payload
  // We assume the light is BLOCKED (true) by default.
  ShadowPayload shadowPayload;
  shadowPayload.isHit = true;

  // 2. Trace
  // Flags:
  // - RAY_FLAG_FORCE_OPAQUE: Prevents execution of AnyHit (alpha testing) for
  // speed.
  // - RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH: Stops traversal at the first
  // occlusion.
  // - RAY_FLAG_SKIP_CLOSEST_HIT_SHADER: Don't run material code; we only care
  // IF it hit.
  TraceRay(topLevelAS,
           RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
               RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
           0xff,  // Instance mask
           0,     // Ray contribution to hit group
           0,     // Multiplier for hit group
           1,  // MISS INDEX: Targets your shadow.miss.slang (SBT Miss Index 1)
           shadowRay, shadowPayload);

  // If isHit is false, it means we reached the Miss Shader = Light is visible!
  return shadowPayload.isHit ? 0.0 : 1.0;
}
#endif
