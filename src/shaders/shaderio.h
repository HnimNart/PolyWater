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

#pragma once

#ifdef __cplusplus
#  define CHECK_STRUCT_ALIGNMENT(_s) static_assert(sizeof(_s) % 8 == 0);
#elif defined(__SLANG__)
#  define CHECK_STRUCT_ALIGNMENT(_s)
#else
#  define CHECK_STRUCT_ALIGNMENT(_s)

// This is a utility to define a buffer reference in GLSL.
// Usage: declare the buffer reference type with: BUFFER_REF_DECL(type), where
// type is the type of the buffer (vec3, float, Material). Then use the buffer
// reference in the shader with: BUFFER_REF(type, address), where address is the
// address of the buffer in the shader.
#  define BUFFER_REF_DECL(_type)                                               \
    layout(buffer_reference, scalar) buffer _type##Buffer                      \
    {                                                                          \
      _type o[];                                                               \
    };

#  define BUFFER_REF(_type, _addr) _type##Buffer(_addr).o

#endif

#include <nvshaders/slang_types.h>

#include <nvshaders/sky_io.h.slang>

enum class MaterialType : uint16_t
{
  eDiffuse,
  eGltfPbr,
  eCount  // Still works as a helper
};

NAMESPACE_SHADERIO_BEGIN()

// Binding Points
enum BindingPoints
{
  eTextures = 0,    // Binding point for textures
  eTlas = 1,        // Top-level acceleration structure
  eOutImage = 2,    // Binding point for output image
  eAccumImage = 3,  //
};

// GLTF
struct BufferView
{
  uint32_t offset;      // Offset in the buffer where the data starts (in bytes)
  uint32_t count;       // Number of elements in the buffer view
  uint32_t byteStride;  // Stride in bytes between consecutive elements (0 if
                        // tightly packed)
};

struct TriangleMesh
{
  BufferView indices;    // Index buffer view
  BufferView positions;  // Position buffer view (vec3)
  BufferView normals;    // Normal buffer view (vec3)
  BufferView colorVert;  // color at vertices (vec4, optional)
  BufferView texCoords;  // texture coordinates buffer view (vec2, optional)
  BufferView tangents;   // tangents buffer view (vec4, optional)
};

struct MeshPrimitive
{
  uint8_t* gltfBuffer =
      nullptr;           // Buffer to the data (index, position, normal, ...)
  TriangleMesh triMesh;  // Mesh data
  int indexType;         // Index type (uint16_t or uint32_t)
  // Workaround for an issue on a Radeon(TM) RX 7900 XT, driver version
  // 32.0.22021.1009, where although GltfMesh has an ArrayStride of 88 (due to
  // the pointer), the GPU treats it as though it has a stride of 84.
  int padWorkaround;
};

struct Instance
{
  float4x4 transform;      // Transform matrix for the instance (local to world)
  uint32_t materialIndex;  // Material properties for the instance
  uint32_t meshIndex;      // Index of the mesh in the GltfMesh vector
  MaterialType hit_group;  // The shader used for this material;
  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Instance)

struct Material
{
  float4 baseColorFactor;  // Base color factor (RGBA)
  float metallicFactor;  // Metallic factor (0.0 = non-metallic, 1.0 = metallic)
  float roughnessFactor;      // Roughness factor (0.0 = smooth, 1.0 = rough)
  int baseColorTextureIndex;  // Index of the base color texture in the GLTF
                              // file (optional)
  float3 emission = float3(0);
};

enum GltfLightType
{
  ePoint = 0,       // Point light type
  eSpot = 1,        // Spot light type
  eDirectional = 2  // Directional light type
};

struct GltfPunctual
{
  float3 position;   // Position of the punctual light in world space
  float intensity;   // Intensity of the light
  float3 direction;  // Direction of the light (for spot and directional lights)
  int type;          // Type of the light (0 = point, 1 = spot, 2 = directional)
  float3 color;      // Color of the light (RGB)
  float coneAngle;   // Cone angle for spot lights (in radians, 0 for point and
                     // directional lights)
};

struct RenderParams
{
  int nSamples = 1;  // Number of samples pr pass
  int maxBounces = 16;
  int nBouncesRR = 3;
  uint frameIdx;  // For RNG seeding (changes every frame)
};

struct SceneResources
{
  Instance*
      instances;  // Address of the instance buffer containing Instance data
  MeshPrimitive* meshes;  // Address of the mesh buffer containing GltfMesh data
  Material* materials;    // Material properties for the instance
};

struct SceneInfo
{
  float4x4 viewMatrix;      // View matrix for the scene
  float4x4 projMatrix;      // projection matrix for the scene
  float4x4 viewProjMatrix;  // View projection matrix for the scene
  float4x4 projInvMatrix;   // Inverse projection matrix for the scene
  float4x4 viewInvMatrix;   // Inverse view matrix for the scene
  float3 cameraPosition;    // Camera position in world space

  // Light info
  int useSky;              // Whether to use the sky rendering
  float3 backgroundColor;  // Background color of the scene (used when not using
                           // sky)
  int numLights;           // Number of punctual lights in the scene (up to 2)
  GltfPunctual
      punctualLights[2];  // Array of punctual lights in the scene (up to 2)
  SkySimpleParameters skySimpleParam;  // Parameters for the sky rendering
};
CHECK_STRUCT_ALIGNMENT(SceneInfo)

struct PushConstant
{
  float3x3 normalMatrix;
  int instanceIndex;                 // Instance index for the current draw call
  SceneInfo* sceneInfoAddress;       // Address of the scene information buffer
  SceneResources* resourcesAddress;  //
  RenderParams renderParams;
};

NAMESPACE_SHADERIO_END()
