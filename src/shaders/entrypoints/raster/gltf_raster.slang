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

  SceneInfo   sceneInfo;
  Instance    instance;
  MeshPrimitive meshIo;

  // On Metal use explicitly-bound buffers (no BDA pointer chain needed).
  // On Vulkan/SPIRV keep the original Buffer Device Address path.
  __target_switch {
  case metal: {
    sceneInfo = g_metalSceneInfoBuf;
    instance  = g_metalInstancesBuf[instanceIndex];
    meshIo    = g_metalMeshesBuf[instance.meshIndex];
  }
  default: {
    SceneInfo *si = pushConst.sceneInfoAddress;
    sceneInfo = *si;
    SceneResources *sceneResources = pushConst.resourcesAddress;
    instance  = sceneResources.instances[instanceIndex];
    meshIo    = sceneResources.meshes[instance.meshIndex];
  }
  }

  float3 posMesh;
  float3 normal;
  float2 texCoord;

  // On Metal read vertex attributes from the interleaved vertex buffer
  // bound at slot 5 by MetalRasterPass.  On Vulkan use BDA-based fetch.
  __target_switch {
  case metal: {
    MetalVertex mv = g_metalVertexBuf[vertexIndex];
    posMesh  = mv.position;
    normal   = mv.normal;
    texCoord = mv.texcoord;
  }
  default: {
    posMesh  = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.positions, vertexIndex);
    normal   = getAttribute<float3>(meshIo.buffer, meshIo.triMesh.normals,   vertexIndex);
    texCoord = getAttribute<float2>(meshIo.buffer, meshIo.triMesh.texCoords, vertexIndex);
  }
  }

  float4 pos = mul(float4(posMesh, 1.0), instance.transform);

  FragmentInput output;
  output.debugColor    = colorFromIndex(vertexIndex);
  output.instanceIndex = instanceIndex;
  output.sv_position   = mul(pos, sceneInfo.viewProjMatrix);
  output.worldPos      = pos.xyz;
  output.worldNormal   = normalize(mul(normal, transpose(inverse3x3(float3x3(instance.transform)))));
  output.worldTexCoord = texCoord;

  return output;
}
