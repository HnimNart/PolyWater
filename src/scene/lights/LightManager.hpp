#pragma once

#include "core/DiscretePdf.hpp"
#include "core/logger.hpp"
#include "core/timers.hpp"

#include "scene/Scene.h"
#include "shaders/shared/structs.h"
#include "shaders/shared/tonemapper_io.h.slang"

class LightManager {
public:
  LightManager() = default;

  // Helper to convert RGB to Luminance
  /**********************************************************/
  inline float getLuminance(const glm::vec3 &color)
  /**********************************************************/
  {
    return shaderio::bt709Luminance(color);
  }

  /**********************************************************/
  shaderio::AreaLight
  uploadAreaLights(const Scene &scene,
                   const std::shared_ptr<IDeviceAssets> deviceResources)
  /**********************************************************/
  {
    SCOPED_TIMER_FUNC();
    auto [weights, triangleLights] = extractAreaLights(scene);
    if (triangleLights.empty()) {
      LOGI("No area lights found\n");
      return {nullptr, nullptr, 0};
    }

    // 2. Upload Area Lights Geometry
    size_t lightBytes = triangleLights.size() * sizeof(shaderio::TriangleLight);
    const auto [bufferAddrLight, _1] =
        deviceResources->upload((void *)triangleLights.data(), lightBytes);

    // 3. Upload CDF
    DiscretePDF cdfBuilder(weights);
    const std::vector<float> &areaLightCDF = cdfBuilder.getCdf();
    size_t cdfBytes = areaLightCDF.size() * sizeof(float);
    const auto [bufferAddrCDF, _2] =
        deviceResources->upload((void *)areaLightCDF.data(), cdfBytes);

    return {static_cast<shaderio::TriangleLight *>(bufferAddrLight),
            static_cast<float *>(bufferAddrCDF),
            static_cast<uint32_t>(triangleLights.size())};
  }

  /**********************************************************/
  std::pair<std::vector<float>, std::vector<shaderio::TriangleLight>>
  extractAreaLights(const Scene &scene)
  /**********************************************************/
  {
    std::vector<float> ligthWeights;
    std::vector<shaderio::TriangleLight> sceneAreaLights;

    // Loop through all instances
    for (const auto &instance : scene.instances) {
      if (instance.materialIndex < 0 ||
          instance.materialIndex >= scene.materials.size()) {
        continue;
      }

      if (instance.hit_group != MaterialType::eEmissive) {
        continue;
      }

      const auto &mat = scene.materials[instance.materialIndex];
      const glm::vec3 &emission = mat.emission;
      float emissionLum = getLuminance(mat.emission);
      if (emissionLum <= 1e-6f) {
        continue;
      }

      // --- Mesh Data Retrieval ---
      if (instance.meshIndex < 0 || instance.meshIndex >= scene.meshes.size())
        continue;
      const auto &prim = scene.meshes[instance.meshIndex];

      // Ensure the raw buffer index is valid
      if (prim.rawBufferIndex >= scene.meshData.size()) {
        continue;
      }

      // Get the base pointer to the CPU data blob
      const std::vector<uint8_t> &rawBuffer =
          scene.meshData[prim.rawBufferIndex];
      const uint8_t *bufferPtr = rawBuffer.data();

      // Calculate number of triangles
      size_t triangleCount = prim.triMesh.indices.count / 3;
      for (uint32_t t = 0; t < triangleCount; ++t) {

        // 1. Get Indices (Handles stride and u16/u32 automatically)
        glm::uvec3 indices = getTriangleIndices(prim, bufferPtr, t);

        // 2. Get Local Positions (Handles stride and offsets automatically)
        glm::vec3 p0 = getAttribute<glm::vec3>(prim, bufferPtr, indices.x);
        glm::vec3 p1 = getAttribute<glm::vec3>(prim, bufferPtr, indices.y);
        glm::vec3 p2 = getAttribute<glm::vec3>(prim, bufferPtr, indices.z);

        // 3. Transform to World Space
        glm::vec3 v0 = glm::vec3(instance.transform * glm::vec4(p0, 1.0f));
        glm::vec3 v1 = glm::vec3(instance.transform * glm::vec4(p1, 1.0f));
        glm::vec3 v2 = glm::vec3(instance.transform * glm::vec4(p2, 1.0f));

        // 4. Calculate Area
        float area = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));

        // 5. Store valid lights
        if (area > 1e-6f) {
          // Power = Area * Radiance * PI
          float power = area * emissionLum * M_PI;
          sceneAreaLights.push_back({v0, v1, v2, emission, area});
          ligthWeights.push_back(power);
        }
      }
    }
    return {ligthWeights, sceneAreaLights};
  }
};
