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

#include "sky_io.h.slang"
#include "slang_types.h"

#define MAX_LIGHTS 2
#ifndef MAX_SCENE_TEXTURES
#define MAX_SCENE_TEXTURES 4096
#endif
#define MAX_SCENE_MESHLETS (10000000)

enum class MaterialType : uint32_t
{
  eDiffuse,
  eGltfPbr,
  eNormals,
  eDieletrics,
  eMirror,
  eVolumetric,
  eEmissive,
  eCount
};

NAMESPACE_SHADERIO_BEGIN()

// Ray payload structure - carries data through the ray tracing pipeline
struct HitPayload
{
  float3 color;   // Accumulated color along the ray path
  float3 weight;  // Weight/importance of this ray (for importance sampling)
  int depth;      // Current recursion depth (for limiting bounces)
  int seed;
  int emit;  // Should we include emitting surfaces in the contribution
  float3 nextRayOrigin;  // Where the bounce starts
  float3 nextRayDir;     // Where the bounce goes
  bool stop;             // "Did we hit the sky or a black hole?"
};

struct ShadowPayload
{
  bool isHit;
};

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

struct GPUMeshlet
{
  uint32_t vertexOffset;    // Offset into the meshlet_vertices buffer
  uint32_t triangleOffset;  // Offset into the meshlet_triangles buffer
  uint32_t vertexCount;     // Number of vertices in this meshlet (max 64)
  uint32_t triangleCount;   // Number of triangles in this meshlet (max 124)

  // Culling data
  float3 center;
  float radius;
  float3 coneAxis;
  float coneCutoff;
};
CHECK_STRUCT_ALIGNMENT(GPUMeshlet)

struct GlobalMeshletRef
{
  uint instanceIndex;
  uint localMeshletIndex;
  uint32_t pad0 = 0;  // Padding to 16-byte boundary (8 → 16 bytes)
  uint32_t pad1 = 0;
};
CHECK_STRUCT_ALIGNMENT(GlobalMeshletRef)

struct MeshletTopology
{
  BufferView meshlets;          // Points to an array of GPUMeshlet structs
  BufferView meshletVertices;   // Points to an array of uint32_t
  BufferView meshletTriangles;  // Points to an array of uint8_t
  BufferView pad;
};
CHECK_STRUCT_ALIGNMENT(MeshletTopology)

struct MeshPrimitive
{
  DevicePtr<uint8_t> buffer = {
      0};                   // GPU device address of the raw mesh data buffer
  TriangleMesh triMesh;     // Mesh data
  MeshletTopology meshlet;  // Meshlet data
  uint32_t rawBufferIndex;  // Index into raw data buffers
  int indexType;            // Index type (uint16_t or uint32_t)
  BoundingBox bbox;         // Local space bbox
};
CHECK_STRUCT_ALIGNMENT(MeshPrimitive)

struct Instance
{
  // Explicit 16-byte blocks for Metal/C++ GPU buffer compatibility.
  // Metal requires float4 at 16-byte alignment and float4x4 at 16-byte
  // alignment. Without this layout the second (and later) mesh reads corrupt
  // data when Metal indexes into a tightly-packed Instance[].

  // Block 1 (offset 0, 16 bytes)
  float3 translation = float3(0);  // 12 bytes
  float pad0 = 0.f;               // 4 bytes → pushes rotation to offset 16

  // Block 2 (offset 16, 16 bytes)
  float4 rotation = float4(0, 0, 0, 1);  // 16 bytes

  // Block 3 (offset 32, 16 bytes)
  float3 scale = float3(1);           // 12 bytes
  uint32_t materialIndex = 0;         // 4 bytes → fills block to 16

  // Block 4 (offset 48, 16 bytes)
  uint32_t meshIndex = 0;             // 4 bytes
  MaterialType hit_group = MaterialType::eDiffuse;  // 4 bytes (uint32_t)
  uint32_t pad1 = 0;                  // 4 bytes
  uint32_t pad2 = 0;                  // 4 bytes → pushes transform to offset 64

  // Blocks 5-8 (offset 64, 64 bytes)
  float4x4 transform;  // Cached Local-to-World matrix (T * R * S)
};
CHECK_STRUCT_ALIGNMENT(Instance)

struct Material
{
  // --- 16-byte aligned (float4) ---
  float4 baseColorFactor;  // Base color factor (RGBA)

  // --- 12/16-byte aligned (float3) ---
  float3 ior = float3(1.5);        // Index of Refraction (RGB for dispersion)
  float3 asymmetry = float3(0.0);  // Anisotropy factor 'g'
  float3 emission = float3(0);     // Emission color

  // --- 4-byte aligned (scalars) ---
  float metallicFactor;       // 0.0 = dielectric, 1.0 = metal
  float roughnessFactor;      // 0.0 = smooth, 1.0 = rough
  float3 sigma_t;             // Extinction coefficient (density)
  int baseColorTextureIndex;  // Texture ID

  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Material)

enum LightType
{
  ePoint = 0,        // Point light type
  eSpot = 1,         // Spot light type
  eDirectional = 2,  // Directional light type
  eAreaLight = 3
};

struct PunctualLight
{
  float3 position;   // Position of the punctual light in world space
  float intensity;   // Intensity of the light
  float3 direction;  // Direction of the light (for spot and directional lights)
  LightType type;    // Type of the light (0 = point, 1 = spot, 2 = directional)
  float3 color;      // Color of the light (RGB)
  float coneAngle;   // Cone angle for spot lights (in radians, 0 for point and
                     // directional lights)
};

struct TriangleLight
{
  float3 v0, v1, v2;  // World Space Positions
  float3 emission;
  float area;
  uint pad;
  uint32_t pad0 = 0;  // Padding to 16-byte boundary (56 → 64 bytes)
  uint32_t pad1 = 0;
};
CHECK_STRUCT_ALIGNMENT(TriangleLight)

struct AreaLight
{
  DevicePtr<TriangleLight> triangles = {};
  uint TriangleLightBufferIndex = -1;
  DevicePtr<float> cdf = {};
  uint cdfBufferIndex = -1;
  uint nTriangles = 0;
  float totalSum = 0;  // Total sum of light
};

struct EnvmapLight
{
  // GPU Buffer Addresses for MIS
  DevicePtr<float> cdfRows = {};  // Conditional CDF: (width + 1) * height
  int cdfRowsBufferIndex = -1;
  DevicePtr<float> cdfCols = {};  // Marginal CDF: (height + 1)
  int cdfColsBufferIndex = -1;

  // Transformation & Intensity
  float rotationAzimuthDegree;
  float4x4 rotation;  // Pre-computed rotation matrix (world to local)
  float scale;        // Intensity/Brightness multiplier
  float totalSum;     // The integral of the importance map (needed for PDF)

  // Texture Information
  uint32_t envTextureIdx = -1;  // Index for bindless texture lookup
  uint2 dims;                   // Width and Height of the texture
  uint32_t pad0 = 0;            // Padding to 16-byte boundary (120 → 128 bytes)
  uint32_t pad1 = 0;
};
CHECK_STRUCT_ALIGNMENT(EnvmapLight)

struct RenderParams
{
  int nSamples = 1;  // Number of samples pr pass
  int maxBounces = 16;
  int nBouncesRR = 3;
  uint frameIdx;

  uint denoise = 0;  // 0 = Off, 1 = Bilateral Filter, (2 = SVGF later, etc.)
  // --- Denoiser Settings ---
  float denoiseRadius = 2.0f;  // Cast to int in shader
  float denoiseSpatialSigma = 2.0f;
  float denoiseLuminanceSigma = 0.5f;
};

struct RasterParams
{
  bool wireframe = false;
  float wireframeLineWidth = 1.0f;
};

struct SceneResources
{
  Instance* instances;    // Address of the instance buffer
  MeshPrimitive* meshes;  // Address of the mesh buffer
  Material* materials;    // Address of material properties
};

struct SceneInfo
{
  float4x4 viewMatrix;      // View matrix for the scene
  float4x4 projMatrix;      // projection matrix for the scene
  float4x4 viewProjMatrix;  // View projection matrix for the scene
  float4x4 projInvMatrix;   // Inverse projection matrix for the scene
  float4x4 viewInvMatrix;   // Inverse view matrix for the scene
  float3 cameraPosition;    // Camera position in world space
  float nearZ;
  float4 frustumPlanes[6];  // Frustum planes

  // Light info
  int useSky;  // Whether to use the sky rendering
  int useEnv;
  float3 backgroundColor;  // Background color of the scene
  float totalAnalyticalPower = 0.0;
  int numLights;  // Number of punctual lights in the scene (up to 2)
  PunctualLight
      punctualLights[MAX_LIGHTS];      // punctual lights in the scene (up to 2)
  SkySimpleParameters skySimpleParam;  // Parameters for the sky rendering

  AreaLight areaLight;
  EnvmapLight envmapLight;
};
CHECK_STRUCT_ALIGNMENT(SceneInfo)

struct PushConstant
{
  float3x3 normalMatrix;
  int instanceIndex;  // Instance index for the current draw call
  DevicePtr<SceneInfo>
      sceneInfoAddress;  // Address of the scene information buffer
  DevicePtr<SceneResources> resourcesAddress;  //
  RenderParams renderParams;
  RasterParams rasterParams;

  // --- NEW: Global Meshlet Dispatch Data ---
  GlobalMeshletRef*
      globalMeshletRefsAddress;  // 64-bit GPU pointer to this frame's array
  uint32_t totalSceneMeshlets;   // How many meshlets we are drawing
  uint32_t pad_meshlet;

  uint2 screenResolution;
};

NAMESPACE_SHADERIO_END()
