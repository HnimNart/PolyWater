

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


// Vertex shader
[shader("vertex")] FragmentInput vertexMain(uint vertexIndex : SV_VertexID) {
  int instanceIndex = pushConst.instanceIndex;
  // 1. Direct dereference of the SceneInfo pointer
  // Slang maps this to a direct 'device' memory load in Metal MSL
  SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  SceneResources *resPtr = pushConst.resourcesAddress;
  Instance instance = resPtr->instances[instanceIndex];
  MeshPrimitive meshIo = resPtr->meshes[instance.meshIndex];

  float3 posMesh;
  float3 normal;
  float2 texCoord;
  fetchVertexAttributes(vertexIndex, meshIo, posMesh, normal, texCoord);

  float4 pos = mul(float4(posMesh, 1.0), (pushConst.transform));

  FragmentInput output;
  output.debugColor = colorFromIndex(instanceIndex);
  output.instanceIndex = instanceIndex;
  output.sv_position = mul(pos, sceneInfo->viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(
      mul(normal, transpose(inverse3x3(float3x3(pushConst.transform)))));
  output.worldTexCoord = texCoord;

  return output;
}
