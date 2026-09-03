#pragma once

#include "shared/slang_types.h"
#include "shared/bindings.h"
#include "shared/structs.h"

#include "include/bsdf/pbr.h.slang"
#include "include/geometry/vertex_fetch.h.slang"
#include "include/light/lights.h.slang"
#include "include/core/color.h.slang"

[[vk::binding(BindRaster::eHiZTexture, 1)]] Texture2D<float> hiZTexture;
[[vk::binding(BindRaster::eHiZSampler, 1)]] SamplerState hiZSampler;


struct FragmentInput
{
  float4 sv_position : SV_Position;
  float3 worldPos : POSITION;
  float3 worldNormal : NORMAL;
  float2 worldTexCoord : TEXCOORD0;
  nointerpolation float4 debugColor: COLOR0;
  nointerpolation uint instanceIndex;
};

struct PSout
{
  float4 color : SV_Target;
};

#define TASK_WORKGROUP_SIZE 32


struct MeshTaskPayload {
    uint meshletIndices[TASK_WORKGROUP_SIZE];
    uint instanceIndices[TASK_WORKGROUP_SIZE]; 
};
