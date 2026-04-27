#pragma once

#include "shared/structs.h"

//-----------------------------------------------------------------------
// Returns 1.0 if visible (lit), 0.0 if occluded (shadow)
//-----------------------------------------------------------------------
/**********************************************************/
float testShadow(float3 worldPos, float3 dirToLight, float distance)
/**********************************************************/
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

