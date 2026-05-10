#pragma once

#include "shared/structs.h"

/**********************************************************/
float testShadow(float3 worldPos, float3 dirToLight, float distance)
/**********************************************************/
{
  RayDesc shadowRay;
  shadowRay.Origin = worldPos;
  shadowRay.Direction = dirToLight;
  shadowRay.TMin = EPSILON;
  shadowRay.TMax = distance - EPSILON;

  ShadowPayload shadowPayload;
  shadowPayload.isHit = true;

  TraceRay(topLevelAS,
           RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH |
               RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
           0xff,
           0,
           0,
           1,
           shadowRay, shadowPayload);

  return shadowPayload.isHit ? 0.0 : 1.0;
}

