/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef NVVKHL_FUNC_H
#define NVVKHL_FUNC_H 1

#include "shared/slang_types.h"
#include "include/constants.h.slang"

NAMESPACE_SHADERIO_BEGIN()

inline float square(float x)
{
  return x * x;
}

inline float saturate(float x)
{
  return clamp(x, 0.0F, 1.0F);
}

inline float3 saturate(float3 x)
{
  return clamp(x, float3(0.0F), float3(1.0F));
}

// Return the luminance of a color
inline float luminance(float3 color)
{
  return color.x * 0.2126F + color.y * 0.7152F + color.z * 0.0722F;
}

inline float3 slerp(float3 a, float3 b, float angle, float t)
{
  t             = saturate(t);
  float  sin1   = sin(angle * t);
  float  sin2   = sin(angle * (1.0F - t));
  float  ta     = sin1 / (sin1 + sin2);
  float3 result = lerp(a, b, float3(ta));
  return normalize(result);
}

inline float clampedDot(float3 x, float3 y)
{
  return clamp(dot(x, y), 0.0F, 1.0F);
}

// Use IFloat to ensure this works for float, float2, float3, etc.
T safe_div<T : IFloat>(T x, T y)
{
  T min_val = T(1e-6f);
  return x / (y < min_val ? min_val : y);
}

//-----------------------------------------------------------------------------
// Builds an orthonormal basis: given only a normal vector, returns a
// tangent and bitangent.
//
// This uses the technique from "Improved accuracy when building an orthonormal
// basis" by Nelson Max, https://jcgt.org/published/0006/01/02.
// Any tangent-generating algorithm must produce at least one discontinuity
// when operating on a sphere (due to the hairy ball theorem); this has a
// small ring-shaped discontinuity at normal.z == -0.99998796.
//-----------------------------------------------------------------------------
inline void orthonormalBasis(float3 normal, NVSHADERS_OUT_TYPE(float3) tangent, NVSHADERS_OUT_TYPE(float3) bitangent)
{
  if(normal.z < -0.99998796F)  // Handle the singularity
  {
    tangent   = float3(0.0F, -1.0F, 0.0F);
    bitangent = float3(-1.0F, 0.0F, 0.0F);
    return;
  }
  float a   = 1.0F / (1.0F + normal.z);
  float b   = -normal.x * normal.y * a;
  tangent   = float3(1.0F - normal.x * normal.x * a, b, -normal.x);
  bitangent = float3(b, 1.0f - normal.y * normal.y * a, -normal.y);
}

//-----------------------------------------------------------------------------
// Like orthonormalBasis(), but returns a tangent and tangent sign that matches
// the glTF convention.
inline float4 makeFastTangent(float3 normal)
{
  float3 tangent, unused;
  orthonormalBasis(normal, tangent, unused);
  // The glTF bitangent sign here is 1.f since for
  // normal == vec3(0.0F, 0.0F, 1.0F), we get
  // tangent == vec3(1.0F, 0.0F, 0.0F) and bitangent == vec3(0.0F, 1.0F, 0.0F),
  // so bitangent = cross(normal, tangent).
  return float4(tangent, 1.f);
}

inline float3 rotate(float3 v, float3 k, float theta)
{
  float cos_theta = cos(theta);
  float sin_theta = sin(theta);

  return (v * cos_theta) + (cross(k, v) * sin_theta) + (k * dot(k, v)) * (1.0F - cos_theta);
}


//-----------------------------------------------------------------------
// Return the UV in a lat-long HDR map
//-----------------------------------------------------------------------
inline float2 getSphericalUv(float3 v)
{
  float gamma = asin(-v.y);
  float theta = atan2(v.z, v.x);

  float2 uv = float2(theta * M_1_OVER_PI * 0.5F, gamma * M_1_OVER_PI) + 0.5F;
  return uv;
}

//-----------------------------------------------------------------------
// Return the interpolated value between 3 values and the barycentrics
//-----------------------------------------------------------------------
inline float2 mixBary(float2 a, float2 b, float2 c, float3 bary)
{
  return a * bary.x + b * bary.y + c * bary.z;
}

inline float3 mixBary(float3 a, float3 b, float3 c, float3 bary)
{
  return a * bary.x + b * bary.y + c * bary.z;
}

inline float4 mixBary(float4 a, float4 b, float4 c, float3 bary)
{
  return a * bary.x + b * bary.y + c * bary.z;
}


// Converts an integer to a color.
// This is roughly evenly distributed within [0,1]^3, and the first 10 colors
// are perceptually distinct.
// Larger numbers will show patterns. If you want to eliminate these patterns,
// hash your input before passing it to this function.
// The lower 23 bits of `val` will be used to avoid issues with int-to-float
// rounding.
inline float3 IntegerToColor(uint val)
{
  // This is equivalent to fract(0.5 + lower23Bits(val) * (-1/a, -1/a^2, -1/a^3)),
  // where a is the positive root of a^4 = a + 1.
  return fract(float3(0.18082749F, 0.32895639F, 0.45029952F) * float3(float(val & 0x7FFFFF)) + float3(0.5F));
}


/*-------------------------------------------------------------------------------------------------
# Function `powerHeuristic`
> The power heuristic for multiple importance sampling, with `beta = 2`.

See equation 9.13 of https://graphics.stanford.edu/papers/veach_thesis/thesis.pdf.
-------------------------------------------------------------------------------------------------*/
inline float powerHeuristic(float a, float b)
{
  const float t = a * a;
  return t / (b * b + t);
}

// Tests a local-space bounding sphere against 6 world-space frustum planes.
// Returns true if the sphere is visible (inside or intersecting the frustum), false if culled.
bool frustumCullSphere(float3 localCenter, float localRadius, float4x4 transform, float4 frustumPlanes[6])
{
    // 1. Transform Center to World Space
    float3 worldCenter = mul(float4(localCenter, 1.0), transform).xyz;

    // 2. Extract Max Scale
    float scaleX = length(float3(transform[0][0], transform[1][0], transform[2][0]));
    float scaleY = length(float3(transform[0][1], transform[1][1], transform[2][1]));
    float scaleZ = length(float3(transform[0][2], transform[1][2], transform[2][2]));
    float maxScale = max(scaleX, max(scaleY, scaleZ));
    
    float worldRadius = localRadius * maxScale;

    // 3. Frustum Culling Test
    for (int i = 0; i < 6; ++i) 
    {
        float distanceToPlane = dot(frustumPlanes[i].xyz, worldCenter) + frustumPlanes[i].w;
        if (distanceToPlane < -worldRadius) 
        {
            return false; // Completely outside this plane
        }
    }
    
    return true; // Survived all planes, it is visible!
}


NAMESPACE_SHADERIO_END()

#endif  // NVVKHL_FUNC_H
