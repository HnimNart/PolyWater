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
[shader("vertex")] FragmentInput vertexMain(uint vertexIndex : SV_VertexID) {
  int instanceIndex = 0;

  // 1. Get the raw address value for comparison
  // We cast the pointer to a 64-bit unsigned integer

  // Define your threshold (example: a specific memory boundary)
  // Note: 0x100000000 is just an example value

  // 2. Resource fetching
  SceneInfo sceneInfo = *pushConst.sceneInfoAddress;
  SceneResources resPtr = *pushConst.resourcesAddress;
  Instance instance = resPtr.instances[instanceIndex];
  MeshPrimitive meshIo = resPtr.meshes[instance.meshIndex];

  float3 posMesh, normal;
  float2 texCoord;
  fetchVertexAttributes(vertexIndex, meshIo, posMesh, normal, texCoord);

  float4x4 myMatrix = float4x4(0.7, 0.0, 0.0, 0.0, // Row 0
                               0.0, 0.7, 0.0, 0.0, // Row 1
                               0.0, 0.0, 0.7, 0.0, // Row 2
                               0.0, 0.4, 0.0, 1.0  // Row 3 (Translation/W-axis)

  );


  float4x4 trans = pushConst.transform;


  // red, red, green, red
  // red, red, red, green
  // red, red, red, red
  // red, red, red, red

  float val = instance.transform[0][2];
  float threshold = 0.70;
  float4 pos = mul(float4(posMesh, 1.0), (trans));

  FragmentInput output;

  // 3. Output two colors based on the pointer value
  if (val < threshold) {
    output.debugColor = float4(1.0, 0.0, 0.0, 1.0); // Red if address is "low"
  } else {
    output.debugColor =
        float4(0.0, 1.0, 0.0, 1.0); // Green if address is "high"
  }

  output.instanceIndex = instanceIndex;
  output.sv_position = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos = pos.xyz;
  output.worldNormal = normalize(
      mul(normal, (float3x3)instance.transform)); // Simplified for example
  output.worldTexCoord = texCoord;

  return output;
}
