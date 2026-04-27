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

#ifndef SLANG_TYPES_H
#define SLANG_TYPES_H

// This header provides type definitions and aliases to bridge between Slang
// shader types, C++ GLM types, and GLSL types. It enables seamless data sharing
// between shader code and host code while maintaining type safety.

#ifdef __cplusplus
#include <glm/glm.hpp>

// In C++, we put all of the shared types and functions into the 'shaderio'
// namespace. We provide the below macros to deal with the fact that not all
// languages #include'ing this header actually support namespaces.
#define NAMESPACE_SHADERIO_BEGIN() namespace shaderio {
#define NAMESPACE_SHADERIO_END() } // namespace shaderio

NAMESPACE_SHADERIO_BEGIN()

using namespace glm; // import all of glm into the shaderio namespace

// GPU device address: a 64-bit integer holding a Vulkan buffer device address.
// On the C++ side this is a plain uint64_t (matches VkDeviceAddress).
// On the GPU/Slang side it is typedef'd to uint8_t* so that byte-level
// pointer arithmetic and pointer casts work naturally in shader code.
template <typename T> struct DevicePtr {
  uint64_t address;
};

// Type aliases to match Slang shader types with C++ GLM types
using float4x4 = glm::mat4;
using float4x3 = glm::mat4x3;
using float3x4 = glm::mat3x4;
using float3x3 = glm::mat3;
using float2x2 = glm::mat2;
using float2x3 = glm::mat2x3;
using float3x2 = glm::mat3x2;

using float2 = glm::vec2;
using float4 = glm::vec4;
using float3 = glm::vec3;

using int2 = glm::ivec2;
using int3 = glm::ivec3;
using int4 = glm::ivec4;

using uint = unsigned int;
using uint2 = glm::uvec2;
using uint3 = glm::uvec3;
using uint4 = glm::uvec4;

using bool2 = glm::bvec2;
using bool3 = glm::bvec3;
using bool4 = glm::bvec4;

//--------------------------------
// Functions
//--------------------------------

// Linear interpolation between two values a and b using parameter t in [0,1]
template <typename T> T lerp(T a, T b, T t) { return glm::mix(a, b, t); }

template <glm::length_t N, typename ScalarType, glm::qualifier Precision>
glm::vec<N, ScalarType, Precision>
mul(glm::vec<N, ScalarType, Precision> v,
    glm::mat<N, N, ScalarType, Precision> M) {
  return M * v;
}

template <glm::length_t N, typename ScalarType, glm::qualifier Precision>
glm::vec<N, ScalarType, Precision> mul(glm::mat<N, N, ScalarType, Precision> M,
                                       glm::vec<N, ScalarType, Precision> v) {
  return v * M;
}

template <glm::length_t N, typename ScalarType, glm::qualifier Precision>
glm::mat<N, N, ScalarType, Precision>
mul(glm::mat<N, N, ScalarType, Precision> A,
    glm::mat<N, N, ScalarType, Precision> B) {
  return B * A;
}

#define SLANG_DEFAULT(x) = (x)

#ifndef NVSHADERS_OUT_TYPE
#define NVSHADERS_OUT_TYPE(T) T &
#endif
#ifndef NVSHADERS_INOUT_TYPE
#define NVSHADERS_INOUT_TYPE(T) T &
#endif

NAMESPACE_SHADERIO_END()

#elif defined(GL_core_profile) // GLSL

#define NAMESPACE_SHADERIO_BEGIN()
#define NAMESPACE_SHADERIO_END()

// GLSL type definitions
#define float4x4 mat4
#define float4x3 mat4x3
#define float3x4 mat3x4
#define float3x3 mat3
#define float2x2 mat2
#define float2x3 mat2x3
#define float3x2 mat3x2

#define float2 vec2
#define float3 vec3
#define float4 vec4

#define int2 ivec2
#define int3 ivec3
#define int4 ivec4

#define uint2 uvec2
#define uint3 uvec3
#define uint4 uvec4

#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4

// Functions
#define lerp mix
#define atan2 atan
#define asuint floatBitsToUint
#define asfloat uintBitsToFloat

#define static
#define inline

#define SLANG_DEFAULT(x)

#ifndef NVSHADERS_OUT_TYPE
#define NVSHADERS_OUT_TYPE(T) out T
#endif

#ifndef NVSHADERS_INOUT_TYPE
#define NVSHADERS_INOUT_TYPE(T) inout T
#endif

vec3 mul(vec3 a, mat3 b) { return b * a; }

mat3 mul(mat3 a, mat3 b) { return b * a; }

#elif __SLANG__

#define NAMESPACE_SHADERIO_BEGIN()
#define NAMESPACE_SHADERIO_END()

struct DevicePtr<T> {
  uint64_t address;

  __init() { address = 0u; }
  __init(uint64_t addr) { address = addr; }

  Ptr<T> get() { return reinterpret<Ptr<T>>(address); }
  __generic<U> Ptr<U> get() { return reinterpret<Ptr<U>>(address); }
  Ptr<T> at(uint64_t byteOffset) {
    return reinterpret<Ptr<T>>(address + byteOffset);
  }
  __generic<U> Ptr<U> at(uint64_t byteOffset) {
    return reinterpret<Ptr<U>>(address + byteOffset);
  }
  T readAt(uint64_t byteOffset) { return *at(byteOffset); }
  __generic<U> U readAt(uint64_t byteOffset) { return *at<U>(byteOffset); }
}

#define SLANG_DEFAULT(x) = (x)
__intrinsic_op(cmpGT) public vector<bool, N> greaterThan<T, let N : int>(
    vector<T, N> x, vector<T, N> y);

T *castAddress<T>(uint64_t addr) { return reinterpret<T *>(addr); }

#ifndef NVSHADERS_OUT_TYPE
#define NVSHADERS_OUT_TYPE(T) out T
#endif

#ifndef NVSHADERS_INOUT_TYPE
#define NVSHADERS_INOUT_TYPE(T) inout T
#endif

#else // No language specified

#error "Unknown language environment"

#endif // __cplusplus

NAMESPACE_SHADERIO_BEGIN()
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

NAMESPACE_SHADERIO_END()

#endif // SLANG_TYPES_H
