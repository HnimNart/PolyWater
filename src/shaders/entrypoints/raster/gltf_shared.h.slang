#pragma once

#include "shared/slang_types.h"

#include "include/bsdf/pbr.h.slang"
#include "include/geometry/vertex_fetch.h.slang"
#include "include/light/lights.h.slang"
#include "shared/bindings.h"
#include "shared/structs.h"
#include "include/core/color.h.slang"


// Output of the mesh shader 
struct FragmentInput
{
  float4 sv_position : SV_Position;
  float3 worldPos : POSITION;
  float3 worldNormal : NORMAL;
  float2 worldTexCoord : TEXCOORD0;
  nointerpolation float4 debugColor: COLOR0;
  nointerpolation uint instanceIndex;
};

// Output of the fragment shader
struct PSout
{
  float4 color : SV_Target;
};

// Define the Payload structure
#define TASK_WORKGROUP_SIZE 32


// Update the payload to hold BOTH the meshlet index and its instance index
struct MeshTaskPayload {
    uint meshletIndices[TASK_WORKGROUP_SIZE];
    uint instanceIndices[TASK_WORKGROUP_SIZE]; 
};
