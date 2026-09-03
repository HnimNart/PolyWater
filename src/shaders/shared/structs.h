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

#define BUFFER_REF_DECL(_type)                                                 \
  layout(buffer_reference, scalar) buffer _type##Buffer                        \
  {                                                                            \
    _type o[];                                                                 \
  };

#define BUFFER_REF(_type, _addr) _type##Buffer(_addr).o

#endif

#include "sky_io.h.slang"
#include "slang_types.h"

#define MAX_LIGHTS 2
#define MAX_SCENE_TEXTURES 4096
#define MAX_SCENE_MESHLETS (10000000)

enum class MaterialType : uint16_t
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

// Ray payload structure
struct HitPayload
{
  float3 color;
  float3 weight;
  int depth;
  int seed;
  int emit;
  float3 nextRayOrigin;
  float3 nextRayDir;
  bool stop;
  float3 albedo;
  float3 normal;
};

struct ShadowPayload
{
  bool isHit;
};

struct BufferView
{
  uint32_t offset;
  uint32_t count;
  uint32_t byteStride;
};

struct TriangleMesh
{
  BufferView indices;
  BufferView positions;
  BufferView normals;
  BufferView colorVert;
  BufferView texCoords;
  BufferView tangents;
};

struct GPUMeshlet
{
  uint32_t vertexOffset;
  uint32_t triangleOffset;
  uint32_t vertexCount;
  uint32_t triangleCount;

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
};
CHECK_STRUCT_ALIGNMENT(GlobalMeshletRef)

struct MeshletTopology
{
  BufferView meshlets;
  BufferView meshletVertices;
  BufferView meshletTriangles;
  BufferView pad;
};
CHECK_STRUCT_ALIGNMENT(MeshletTopology)

struct MeshPrimitive
{
  DevicePtr<uint8_t> buffer = {0};
  TriangleMesh triMesh;
  MeshletTopology meshlet;
  uint32_t rawBufferIndex;
  int indexType;
  BoundingBox bbox;
};
CHECK_STRUCT_ALIGNMENT(MeshPrimitive)

struct Instance
{
  float3 translation = float3(0);
  float4 rotation = float4(0, 0, 0, 1);
  float3 scale = float3(1);
  float4x4 transform;
  uint32_t materialIndex;
  uint32_t meshIndex;
  MaterialType hit_group;
  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Instance)

struct Material
{
  float4 baseColorFactor;

  float3 ior = float3(1.5);
  float3 asymmetry = float3(0.0);
  float3 emission = float3(0);

  float metallicFactor;
  float roughnessFactor;
  float3 sigma_t;
  int baseColorTextureIndex;

  uint32_t pad;
};
CHECK_STRUCT_ALIGNMENT(Material)

enum LightType
{
  ePoint = 0,
  eSpot = 1,
  eDirectional = 2,
  eAreaLight = 3
};

struct PunctualLight
{
  float3 position;
  float intensity;
  float3 direction;
  LightType type;
  float3 color;
  float coneAngle;
};

struct TriangleLight
{
  float3 v0, v1, v2;
  float3 emission;
  float area;
  uint pad;
};
CHECK_STRUCT_ALIGNMENT(TriangleLight)

struct AreaLight
{
  DevicePtr<TriangleLight> triangles = {};
  uint TriangleLightBufferIndex = -1;
  DevicePtr<float> cdf = {};
  uint cdfBufferIndex = -1;
  uint nTriangles = 0;
  float totalSum = 0;
};

struct EnvmapLight
{
  DevicePtr<float> cdfRows = {};
  int cdfRowsBufferIndex = -1;
  DevicePtr<float> cdfCols = {};
  int cdfColsBufferIndex = -1;

  float rotationAzimuthDegree;
  float4x4 rotation;
  float scale;
  float totalSum;

  uint32_t envTextureIdx = -1;
  uint2 dims;
};
CHECK_STRUCT_ALIGNMENT(EnvmapLight)

struct RenderParams
{
  int nSamples = 1;
  int maxBounces = 16;
  int nBouncesRR = 3;
  uint frameIdx;

  // TODO Remove this
  uint denoise = 0;
  float denoiseRadius = 2.0f;
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
  Instance* instances;
  MeshPrimitive* meshes;
  Material* materials;
};

struct SceneInfo
{
  float4x4 viewMatrix;
  float4x4 projMatrix;
  float4x4 viewProjMatrix;
  float4x4 projInvMatrix;
  float4x4 viewInvMatrix;
  float3 cameraPosition;
  float nearZ;
  float4 frustumPlanes[6];

  int useSky;
  int useEnv;
  float3 backgroundColor;
  float totalAnalyticalPower = 0.0;
  int numLights;
  PunctualLight punctualLights[MAX_LIGHTS];
  SkySimpleParameters skySimpleParam;

  AreaLight areaLight;
  EnvmapLight envmapLight;
};
CHECK_STRUCT_ALIGNMENT(SceneInfo)

struct PushConstant
{
  float3x3 normalMatrix;
  int instanceIndex;
  DevicePtr<SceneInfo> sceneInfoAddress;
  DevicePtr<SceneResources> resourcesAddress;
  RenderParams renderParams;
  RasterParams rasterParams;

  GlobalMeshletRef* globalMeshletRefsAddress;
  uint32_t totalSceneMeshlets;
  uint32_t pad_meshlet;

  uint2 screenResolution;
};

NAMESPACE_SHADERIO_END()
