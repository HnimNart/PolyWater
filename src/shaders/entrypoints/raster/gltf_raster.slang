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

// Per-vertex attributes to be assembled from bound vertex buffers.
struct VSin {
  float3 position : POSITION;
  float3 normal : NORMAl;
};

// Vertex  Shader
[shader("vertex")] FragmentInput vertexMain(VSin input, uint vertexIndex
                                            : SV_VertexID) {
  int instanceIndex = pushConst.instanceIndex;

  SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  SceneResources *sceneResources = pushConst.resourcesAddress;

  Instance instance = sceneResources.instances[instanceIndex];
  MeshPrimitive meshIo = sceneResources.meshes[instance.meshIndex];
  Material material = sceneResources.materials[instance.materialIndex];

  // Retrieve the data
  float3 posMesh = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions,
                                        vertexIndex);
  float3 normal =
      getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals, vertexIndex);
  float2 texCoord = getAttribute<float2>(meshIo.buffer,
                                         meshIo.triMesh.texCoords, vertexIndex);

  float4 pos = mul(float4(posMesh, 1.0), instance.transform);

  FragmentInput output;
  output.debugColor = colorFromIndex(vertexIndex); // Example custom logic
  output.instanceIndex = instanceIndex;
  output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(mul(normal, float3x3(pushConst.normalMatrix)));
  output.worldTexCoord = texCoord;

  return output;
}
