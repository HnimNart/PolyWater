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

#include "slang_types.h"

#include "sky_io.h.slang"

#define MAX_LIGHTS 2
#define MAX_SCENE_TEXTURES 4096
#define MAX_SCENE_MESHLETS (10000000)
#define MAX_SCENE_INSTANCES (10000000)

enum class MaterialType : uint16_t {
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
struct HitPayload {
  float3 color;  // Accumulated color along the ray path
  float3 weight; // Weight/importance of this ray (for importance sampling)
  int depth;     // Current recursion depth (for limiting bounces)
  int seed;
  int emit; // Should we include emitting surfaces in the contribution
  float3 nextRayOrigin; // Where the bounce starts
  float3 nextRayDir;    // Where the bounce goes
  bool stop;            // "Did we hit the sky or a black hole?"
};

struct ShadowPayload {
  bool isHit;
};

struct BoundingBox {
  float3 min;
  float3 max;

#ifdef __cplusplus
  BoundingBox()
      : min(std::numeric_limits<float>::max()),
        max(std::numeric_limits<float>::lowest()) {}

  BoundingBox(float3 _min, float3 _max) : min(_min), max(_max) {}

  // Add a point to the bounding box (Encapsulate)
  void add(const float3 &p) {
    min = glm::min(min, p);
    max = glm::max(max, p);
  }

  // Merge another bounding box into this one
  void add(const BoundingBox &other) {
    min = glm::min(min, other.min);
    max = glm::max(max, other.max);
  }

  bool isEmpty() const {
    return min.x > max.x || min.y > max.y || min.z > max.z;
  }

  float3 center() const { return (min + max) * 0.5f; }
#endif
};

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

struct GPUMeshlet {
  uint32_t vertexOffset;   // Offset into the meshlet_vertices buffer
  uint32_t triangleOffset; // Offset into the meshlet_triangles buffer
  uint32_t vertexCount;    // Number of vertices in this meshlet (max 64)
  uint32_t triangleCount;  // Number of triangles in this meshlet (max 124)

  // Culling data
  float3 center;
  float radius;
  float3 coneAxis;
  float coneCutoff;
};
CHECK_STRUCT_ALIGNMENT(GPUMeshlet)

struct GlobalMeshletRef {
  uint instanceIndex;
  uint localMeshletIndex;
};
CHECK_STRUCT_ALIGNMENT(GlobalMeshletRef)

struct DrawIndirectCommand {
    uint32_t vertexCount;
    uint32_t instanceCount;
    uint32_t firstVertex;
    uint32_t firstInstance;
};
CHECK_STRUCT_ALIGNMENT(DrawIndirectCommand)

struct MeshletTopology {
  BufferView meshlets; // Points to an array of GPUMeshlet structs
  BufferView
      meshletVertices; // Points to an array of uint32_t (global vertex indices)
  BufferView meshletTriangles; // Points to an array of uint8_t (local triangle
                               // indices)
  BufferView tmp; // Points to an array of uint8_t (local triangle indices)
};
CHECK_STRUCT_ALIGNMENT(MeshletTopology)

struct MeshPrimitive {
  uint8_t *buffer =
      nullptr;             // Buffer to the data (index, position, normal, ...)
  TriangleMesh triMesh;    // Mesh data
  MeshletTopology meshlet; // Meshlet data
  uint32_t rawBufferIndex; // Index into raw data buffers
  int indexType;           // Index type (uint16_t or uint32_t)
  BoundingBox bbox;        // Local space bbox
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
  // --- 16-byte aligned (float4) ---
  float4 baseColorFactor; // Base color factor (RGBA)

  // --- 12/16-byte aligned (float3) ---
  float3 ior = float3(1.5);       // Index of Refraction (RGB for dispersion)
  float3 asymmetry = float3(0.0); // Anisotropy factor 'g'
  float3 emission = float3(0);    // Emission color

  // --- 4-byte aligned (scalars) ---
  float metallicFactor;      // 0.0 = dielectric, 1.0 = metal
  float roughnessFactor;     // 0.0 = smooth, 1.0 = rough
  float3 sigma_t;            // Extinction coefficient (density)
  int baseColorTextureIndex; // Texture ID

  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Material)

enum LightType {
  ePoint = 0,       // Point light type
  eSpot = 1,        // Spot light type
  eDirectional = 2, // Directional light type
  eAreaLight = 3
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

struct TriangleLight {
  float3 v0, v1, v2; // World Space Positions
  float3 emission;
  float area;
  uint pad;
};
CHECK_STRUCT_ALIGNMENT(TriangleLight)

struct AreaLight {
  TriangleLight *triangles = nullptr;
  uint TriangleLightBufferIndex = -1;
  float *cdf = nullptr;
  uint cdfBufferIndex = -1;
  uint nTriangles = 0;
  float totalSum = 0; // Total sum of light
};

struct EnvmapLight {
  // GPU Buffer Addresses for MIS
  float *cdfRows = nullptr; // Conditional CDF: (width + 1) * height
  int cdfRowsBufferIndex = -1;
  float *cdfCols = nullptr; // Marginal CDF: (height + 1)
  int cdfColsBufferIndex = -1;

  // Transformation & Intensity
  float rotationAzimuthDegree;
  float4x4 rotation; // Pre-computed rotation matrix (world to local)
  float scale;       // Intensity/Brightness multiplier
  float totalSum;    // The integral of the importance map (needed for PDF)

  // Texture Information
  uint32_t envTextureIdx = -1; // Index for bindless texture lookup
  uint2 dims;                  // Width and Height of the texture
};
CHECK_STRUCT_ALIGNMENT(EnvmapLight)

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
  float nearZ;
  float4 frustumPlanes[6]; // Frustum planes

  // Light info
  int useSky; // Whether to use the sky rendering
  int useEnv;
  float3 backgroundColor; // Background color of the scene
  float totalAnalyticalPower = 0.0;
  int numLights; // Number of punctual lights in the scene (up to 2)
  PunctualLight
      punctualLights[MAX_LIGHTS];     // punctual lights in the scene (up to 2)
  SkySimpleParameters skySimpleParam; // Parameters for the sky rendering

  AreaLight areaLight;
  EnvmapLight envmapLight;
};
CHECK_STRUCT_ALIGNMENT(SceneInfo)

struct PushConstant {
  float3x3 normalMatrix;
  int instanceIndex;                // Instance index for the current draw call
  SceneInfo *sceneInfoAddress;      // Address of the scene information buffer
  SceneResources *resourcesAddress; //
  RenderParams renderParams;
  RasterParams rasterParams;

  // Global Meshlet Dispatch Data ---
  GlobalMeshletRef *globalMeshletRefsAddress; // 64-bit GPU pointer
  uint32_t totalSceneMeshlets; // How many meshlets we are drawing
  uint32_t pad_meshlet;

  // Raster
  uint64_t instanceMapAddress;
  uint64_t indirectCommandsAddress; // NEW: Address of the indirect buffer
  uint64_t drawCountAddress;        // NEW: Address of the atomic counter buffer
  uint32_t totalSceneInstances;

  uint2 screenResolution;
};

NAMESPACE_SHADERIO_END()
