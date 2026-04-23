/*
 * Copyright (c) 2023-2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gltf_shared.h.slang"

float4x4 convertVulkanToMetal(float4x4 vulkanMat) {
    // 1. Create a Y-flip matrix
    // In HLSL, identity is float4x4(1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1)
    // We negate the [1][1] entry to flip the Y axis scale.
    float4x4 yFlip = float4x4(
        1.0,  0.0, 0.0, 0.0,
        0.0, -1.0, 0.0, 0.0,
        0.0,  0.0, 1.0, 0.0,
        0.0,  0.0, 0.0, 1.0
    );

    // 2. Apply the flip
    // In Vulkan/GLSL: yFlip * vulkanMat
    // In HLSL/Metal: mul(yFlip, vulkanMat)
    float4x4 metalMat = mul(yFlip, vulkanMat);

    // 3. Transpose for Row-Vector math: mul(pos, matrix)
    return transpose(metalMat);
}


// Vertex shader
[shader("vertex")] FragmentInput vertexMain(uint vertexIndex : SV_VertexID) {
  int instanceIndex = pushConst.instanceIndex;

  SceneInfo sceneInfo;
  Instance instance;
  MeshPrimitive meshIo;
  fetchVertexResources(instanceIndex, sceneInfo, instance, meshIo);

  float3 posMesh;
  float3 normal;
  float2 texCoord;
  fetchVertexAttributes(vertexIndex, meshIo, posMesh, normal, texCoord);

  float4 pos = mul(float4(posMesh, 1.0), (instance.transform));

  FragmentInput output;
  output.debugColor = colorFromIndex(vertexIndex);
  output.instanceIndex = instanceIndex;
  output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(
      mul(normal, transpose(inverse3x3(float3x3(instance.transform)))));
  output.worldTexCoord = texCoord;

  return output;
}
