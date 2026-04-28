/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.
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
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

namespace nvutils
{

/*-------------------------------------------------------------------------------------------------

```nvh::Bbox``` is a class to create bounding boxes.
It grows by adding 3d vector, can combine other bound boxes.
And it returns information, like its volume, its center, the min, max, etc..

-------------------------------------------------------------------------------------------------*/
struct Bbox
{
  Bbox() = default;
  Bbox(glm::vec3 _min, glm::vec3 _max) : m_min(_min), m_max(_max)
  {
  }
  Bbox(const std::vector<glm::vec3>& corners);

  void insert(const glm::vec3& v);

  void insert(const Bbox& b);

  inline Bbox& operator+=(float v)
  {
    m_min -= v;
    m_max += v;
    return *this;
  }

  inline bool isEmpty() const
  {
    return m_min == glm::vec3{std::numeric_limits<float>::max()} ||
           m_max == glm::vec3{std::numeric_limits<float>::lowest()};
  }

  uint32_t rank() const;
  inline bool isPoint() const
  {
    return m_min == m_max;
  }
  inline bool isLine() const
  {
    return rank() == 1u;
  }
  inline bool isPlane() const
  {
    return rank() == 2u;
  }
  inline bool isVolume() const
  {
    return rank() == 3u;
  }
  inline glm::vec3 min() const
  {
    return m_min;
  }
  inline glm::vec3 max() const
  {
    return m_max;
  }
  inline glm::vec3 extents()
  {
    return m_max - m_min;
  }
  inline glm::vec3 center() const
  {
    return (m_min + m_max) * 0.5f;
  }
  inline float radius() const
  {
    return glm::length(m_max - m_min) * 0.5f;
  }

  Bbox transform(glm::mat4 mat);

private:
  glm::vec3 m_min{std::numeric_limits<float>::max()};
  glm::vec3 m_max{-std::numeric_limits<float>::max()};
};

/**********************************************************/
template <typename T, typename TFlag> inline bool hasFlag(T a, TFlag flag)
/**********************************************************/
{
  return (a & flag) == flag;
}

}  // namespace nvutils
