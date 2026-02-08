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
#define CHECK_STRUCT_ALIGNMENT(_s) static_assert(sizeof(_s) % 8 == 0);
#elif defined(__SLANG__)
#define CHECK_STRUCT_ALIGNMENT(_s)
#else
#define CHECK_STRUCT_ALIGNMENT(_s)

// This is a utility to define a buffer reference in GLSL.
// Usage: declare the buffer reference type with: BUFFER_REF_DECL(type), where
// type is the type of the buffer (vec3, float, Material). Then use the buffer
// reference in the shader with: BUFFER_REF(type, address), where address is the
// address of the buffer in the shader.
#define BUFFER_REF_DECL(_type)                                                 \
  layout(buffer_reference, scalar) buffer _type##Buffer { _type o[]; };

#define BUFFER_REF(_type, _addr) _type##Buffer(_addr).o

#endif

#include <nvshaders/slang_types.h>

#include <nvshaders/sky_io.h.slang>

#define MAX_LIGHTS 2

enum class MaterialType : uint16_t {
  eDiffuse,
  eGltfPbr,
  eNormals,
  eDieletrics,
  eMirror,
  eCount
};

NAMESPACE_SHADERIO_BEGIN()

// Binding Points
enum BindingPoints {
  eTextures = 0,   // Binding point for textures
  eTlas = 1,       // Top-level acceleration structure
  eOutImage = 2,   // Binding point for output image
  eAccumImage = 3, //
};

// GLTF
struct BufferView {
  uint32_t offset;     // Offset in the buffer where the data starts (in bytes)
  uint32_t count;      // Number of elements in the buffer view
  uint32_t byteStride; // Stride in bytes between consecutive elements (0 if
                       // tightly packed)
};

struct TriangleMesh {
  BufferView indices;   // Index buffer view
  BufferView positions; // Position buffer view (vec3)
  BufferView normals;   // Normal buffer view (vec3)
  BufferView colorVert; // color at vertices (vec4, optional)
  BufferView texCoords; // texture coordinates buffer view (vec2, optional)
  BufferView tangents;  // tangents buffer view (vec4, optional)
};

struct MeshPrimitive {
  uint8_t *buffer =
      nullptr;          // Buffer to the data (index, position, normal, ...)
  TriangleMesh triMesh; // Mesh data
  int indexType;        // Index type (uint16_t or uint32_t)
  float3 boxMin = float3(0);
  float3 boxMax = float3(0);
  // Workaround for an issue on a Radeon(TM) RX 7900 XT, driver version
  // 32.0.22021.1009, where although GltfMesh has an ArrayStride of 88 (due to
  // the pointer), the GPU treats it as though it has a stride of 84.
  int padWorkaround;
};
CHECK_STRUCT_ALIGNMENT(MeshPrimitive)

struct Instance {
  float3 translation = float3(0);       // Position in world space
  float4 rotation = float4(0, 0, 0, 1); // Rotation quaternion (x, y, z, w).
  float3 scale = float3(1);             // Scale factor
  float4x4 transform;     // Cached Local-to-World matrix (T * R * S)
  uint32_t materialIndex; // Index into the materials storage buffer
  uint32_t meshIndex;     // Index into the meshes storage buffer
  MaterialType hit_group; // Shader Binding Table offset (which shaders to run)
  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Instance)

struct Material {
  float4 baseColorFactor; // Base color factor (RGBA)
  float metallicFactor;  // Metallic factor (0.0 = non-metallic, 1.0 = metallic)
  float roughnessFactor; // Roughness factor (0.0 = smooth, 1.0 = rough)
  int baseColorTextureIndex; // Index of the base color texture in the GLTF
                             // file (optional)
  float3 ior = float3(1.5);  // inside ior
  float3 emission = float3(0);
};

enum LightType {
  ePoint = 0,      // Point light type
  eSpot = 1,       // Spot light type
  eDirectional = 2 // Directional light type
};

struct PunctualLight {
  float3 position;  // Position of the punctual light in world space
  float intensity;  // Intensity of the light
  float3 direction; // Direction of the light (for spot and directional lights)
  LightType type;   // Type of the light (0 = point, 1 = spot, 2 = directional)
  float3 color;     // Color of the light (RGB)
  float coneAngle;  // Cone angle for spot lights (in radians, 0 for point and
                    // directional lights)
};

struct RenderParams {
  int nSamples = 1; // Number of samples pr pass
  int maxBounces = 16;
  int nBouncesRR = 3;
  uint frameIdx;
};

struct RasterParams {
  bool wireframe = false;
  float wireframeLineWidth = 1.0f;
};

struct SceneResources {
  Instance *instances;   // Address of the instance buffer
  MeshPrimitive *meshes; // Address of the mesh buffer
  Material *materials;   // Address of material properties
};

struct SceneInfo {
  float4x4 viewMatrix;     // View matrix for the scene
  float4x4 projMatrix;     // projection matrix for the scene
  float4x4 viewProjMatrix; // View projection matrix for the scene
  float4x4 projInvMatrix;  // Inverse projection matrix for the scene
  float4x4 viewInvMatrix;  // Inverse view matrix for the scene
  float3 cameraPosition;   // Camera position in world space

  // Light info
  int useSky;             // Whether to use the sky rendering
  float3 backgroundColor; // Background color of the scene
  int numLights;          // Number of punctual lights in the scene (up to 2)
  PunctualLight
      punctualLights[MAX_LIGHTS];     // punctual lights in the scene (up to 2)
  SkySimpleParameters skySimpleParam; // Parameters for the sky rendering
};
CHECK_STRUCT_ALIGNMENT(SceneInfo)

struct PushConstant {
  float3x3 normalMatrix;
  int instanceIndex;                // Instance index for the current draw call
  SceneInfo *sceneInfoAddress;      // Address of the scene information buffer
  SceneResources *resourcesAddress; //
  RenderParams renderParams;
  RasterParams rasterParams;
};

NAMESPACE_SHADERIO_END()
