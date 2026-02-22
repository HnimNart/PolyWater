#include "LightManager.hpp"

#include <algorithm>
#include <numeric>
#include <stb_image.h>

#include "core/DiscretePdf.hpp"
#include "core/logger.hpp"
#include "core/timers.hpp"

#include "shaders/shared/tonemapper_io.h.slang"

namespace {
/**
 * Helper to convert RGB to Luminance based on BT.709
 */
inline float getLuminance(const glm::vec3 &color) {
  return shaderio::bt709Luminance(color);
}
} // namespace

/**********************************************************/
shaderio::AreaLight LightManager::uploadAreaLights(
    const Scene &scene, const std::shared_ptr<IDeviceAssets> &deviceResources)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  auto [weights, triangleLights] = extractAreaLights(scene);

  if (triangleLights.empty()) {
    LOGD("No area lights found\n");
    return {nullptr, nullptr, 0, 0};
  }

  // 1. Upload Area Lights Geometry
  size_t lightBytes = triangleLights.size() * sizeof(shaderio::TriangleLight);
  std::span<const uint8_t> lightDataView(
      reinterpret_cast<const uint8_t *>(triangleLights.data()), lightBytes);
  const auto lightHandle = deviceResources->upload(lightDataView);

  // 2. Build and Upload CDF for Importance Sampling
  DiscretePDF cdfBuilder(weights);
  const std::vector<float> &areaLightCDF = cdfBuilder.getCdf();

  size_t cdfBytes = areaLightCDF.size() * sizeof(float);
  std::span<const uint8_t> cdfDataView(
      reinterpret_cast<const uint8_t *>(areaLightCDF.data()), cdfBytes);
  const auto cdfHandle = deviceResources->upload(cdfDataView);
  float totalSum = std::accumulate(weights.begin(), weights.end(), 0.0f);

  return {
      .triangles = lightHandle.as<shaderio::TriangleLight>(),
      .cdf = cdfHandle.as<float>(),
      .nTriangles = static_cast<uint32_t>(triangleLights.size()),
      .totalSum = totalSum,
  };
}

/**********************************************************/
std::pair<std::vector<float>, std::vector<shaderio::TriangleLight>>
LightManager::extractAreaLights(const Scene &scene)
/**********************************************************/
{
  std::vector<float> lightWeights;
  std::vector<shaderio::TriangleLight> sceneAreaLights;

  for (const auto &instance : scene.instances) {
    // Validation
    if (instance.materialIndex < 0 ||
        instance.materialIndex >= scene.materials.size())
      continue;
    if (instance.hit_group != MaterialType::eEmissive)
      continue;

    const auto &mat = scene.materials[instance.materialIndex];
    float emissionLum = getLuminance(mat.emission);
    if (emissionLum <= 1e-6f)
      continue;

    if (instance.meshIndex < 0 || instance.meshIndex >= scene.meshes.size())
      continue;

    const auto &prim = scene.meshes[instance.meshIndex];
    if (prim.rawBufferIndex >= scene.meshData.size())
      continue;

    const uint8_t *bufferPtr = scene.meshData[prim.rawBufferIndex].data();
    size_t triangleCount = prim.triMesh.indices.count / 3;

    for (uint32_t t = 0; t < triangleCount; ++t) {
      // Get Indices
      glm::uvec3 indices = getTriangleIndices(prim, bufferPtr, t);

      // Get Local Positions
      glm::vec3 p0 = getAttribute<glm::vec3>(prim, bufferPtr, indices.x);
      glm::vec3 p1 = getAttribute<glm::vec3>(prim, bufferPtr, indices.y);
      glm::vec3 p2 = getAttribute<glm::vec3>(prim, bufferPtr, indices.z);

      // Transform to World Space
      glm::vec3 v0 = glm::vec3(instance.transform * glm::vec4(p0, 1.0f));
      glm::vec3 v1 = glm::vec3(instance.transform * glm::vec4(p1, 1.0f));
      glm::vec3 v2 = glm::vec3(instance.transform * glm::vec4(p2, 1.0f));

      // Calculate Area
      float area = 0.5f * glm::length(glm::cross(v1 - v0, v2 - v0));

      if (area > 1e-6f) {
        // Power approximation for MIS/Sampling weights
        float power = area * emissionLum * M_PI;
        sceneAreaLights.push_back({v0, v1, v2, mat.emission, area});
        lightWeights.push_back(power);
      }
    }
  }
  return {lightWeights, sceneAreaLights};
}

/**********************************************************/
const EnvmapInfo &
LightManager::loadEnvmap(const std::filesystem::path &filename, float scale,
                         float rotation)
/**********************************************************/
{
  if (filename == m_envmapInfo.filename && scale == m_envmapInfo.scale &&
      rotation == m_envmapInfo.rotation) {
    return m_envmapInfo;
  }

  if (filename.empty()) {
    return m_envmapInfo;
  }

  // 1. Load HDR Image into our agnostic CPU struct
  m_envmapInfo.image = core::loadRawImage(filename);

  if (!m_envmapInfo.image.isValid()) {
    LOGE("Failed to load environment map at: %s", filename.string().c_str());
    return m_envmapInfo;
  }

  m_envmapInfo.filename = filename;
  m_envmapInfo.scale = scale;
  m_envmapInfo.rotation = rotation;

  uint32_t width = m_envmapInfo.image.width;
  uint32_t height = m_envmapInfo.image.height;
  float *data = reinterpret_cast<float *>(m_envmapInfo.image.pixels.data());

  // 2. Build Importance Map
  // We multiply luminance by sin(theta) to handle the area distortion
  // of equirectangular maps at the poles.
  size_t numPixels = (size_t)width * height;
  m_envmapInfo.importanceMap.resize(numPixels);
  std::vector<float> rowWeights(height, 0.0f);

  for (uint32_t y = 0; y < height; ++y) {
    // v ranges from 0 to 1; theta from 0 to PI
    float v = (float)y / (float)height;
    float sinTheta = std::sin(M_PI * v);

    float rowSum = 0.0f;
    for (uint32_t x = 0; x < width; ++x) {
      int idx = y * width + x;

      // assetManager->loadRawImage used req_comp = 4 (RGBA)
      glm::vec3 rgb(data[idx * 4 + 0], data[idx * 4 + 1], data[idx * 4 + 2]);

      float importance = getLuminance(rgb) * sinTheta;
      m_envmapInfo.importanceMap[idx] = importance;
      rowSum += importance;
    }
    rowWeights[y] = rowSum;
  }

  // 3. Generate 2D CDF (Importance Sampling)
  // Marginal CDF (Columns/Rows selection)
  m_envmapInfo.cdfCols.resize(height + 1);
  m_envmapInfo.cdfCols[0] = 0.0f;
  for (uint32_t y = 0; y < height; ++y) {
    m_envmapInfo.cdfCols[y + 1] = m_envmapInfo.cdfCols[y] + rowWeights[y];
  }

  // Store the total integral before normalizing!
  // You need this for the PDF calculation: pdf = importance / totalIntegral
  m_envmapInfo.totalIntegral = m_envmapInfo.cdfCols[height];

  if (m_envmapInfo.totalIntegral > 0) {
    for (auto &val : m_envmapInfo.cdfCols)
      val /= m_envmapInfo.totalIntegral;
  }

  // Conditional CDF (Rows/Pixel selection)
  m_envmapInfo.cdfRows.resize((width + 1) * height);
  for (uint32_t y = 0; y < height; ++y) {
    int rowOffset = y * (width + 1);
    m_envmapInfo.cdfRows[rowOffset] = 0.0f;

    for (uint32_t x = 0; x < width; ++x) {
      float importance = m_envmapInfo.importanceMap[y * width + x];
      m_envmapInfo.cdfRows[rowOffset + x + 1] =
          m_envmapInfo.cdfRows[rowOffset + x] + importance;
    }

    float rowTotal = m_envmapInfo.cdfRows[rowOffset + width];
    if (rowTotal > 0) {
      for (uint32_t x = 0; x <= width; ++x) {
        m_envmapInfo.cdfRows[rowOffset + x] /= rowTotal;
      }
    }
  }

  LOGD("Environment map processed: %dx%d", width, height);

  return m_envmapInfo;
}

/**********************************************************/
void LightManager::uploadEnvmap(
    const EnvmapInfo &info,
    const std::shared_ptr<IDeviceAssets> &deviceResources,
    shaderio::EnvmapLight &envmapLight)
/**********************************************************/
{
  SCOPED_TIMER_FUNC();

  // Check if we actually have image data to upload
  if (!info.image.isValid()) {
    return;
  }

  // Only re-upload heavy resources if it's the first time OR the file changed.
  bool first = (envmapLight.envTextureIdx == -1);
  bool needsUpload = first || (info.filename != m_currentEnvFilename);
  if (needsUpload) {
    if (!first) {
      deviceResources->destroyBuffer(envmapLight.cdfRowsBufferIndex);
      deviceResources->destroyBuffer(envmapLight.cdfColsBufferIndex);
    }

    // 1. Upload the HDR Texture (The visual data)
    deviceResources->addAndUploadTexture(info.image, envmapLight.envTextureIdx);

    // 2. Upload CDF Row Data (Conditional CDF)
    size_t cdfRowBytes = info.cdfRows.size() * sizeof(float);
    std::span<const uint8_t> cdfRowView(
        reinterpret_cast<const uint8_t *>(info.cdfRows.data()), cdfRowBytes);
    const auto rowHandle = deviceResources->upload(cdfRowView);
    envmapLight.cdfRows = rowHandle.as<float>();
    envmapLight.cdfRowsBufferIndex = rowHandle.id;

    // 3. Upload CDF Column Data (Marginal CDF)
    size_t cdfColBytes = info.cdfCols.size() * sizeof(float);
    std::span<const uint8_t> cdfColView(
        reinterpret_cast<const uint8_t *>(info.cdfCols.data()), cdfColBytes);
    const auto colCdf = deviceResources->upload(cdfColView);
    envmapLight.cdfCols = colCdf.as<float>();
    envmapLight.cdfColsBufferIndex = colCdf.id;

    envmapLight.scale = info.scale;
    envmapLight.dims = glm::uvec2(info.image.width, info.image.height);
    float rad = glm::radians(info.rotation);
    envmapLight.rotationAzimuthDegree = info.rotation;
    envmapLight.rotation =
        glm::rotate(glm::mat4(1.0f), rad, glm::vec3(0, 1, 0));
    envmapLight.totalSum = info.totalIntegral;

    // Update our tracker
    m_currentEnvFilename = info.filename;
  }
}

/**********************************************************/
float LightManager::computeAnalyticalLightContribution(const Scene &scene)
/**********************************************************/
{
  const shaderio::SceneInfo &sceneInfo = scene.sceneInfo;

  float totalAnalyticalPower{0.0f};
  for (const auto &l : sceneInfo.punctualLights) {

    // 1. Calculate base Luminance
    float luminance = getLuminance(l.color);
    float basePower = luminance * l.intensity;

    float lightPower{0.0f};

    // 2. Calculate total flux based on the geometric spread of the light type
    if (l.type == shaderio::LightType::ePoint) { // ePoint
      lightPower = 4.0f * M_PI * basePower;
    } else if (l.type == shaderio::LightType::eSpot) { // eSpot
      float cosTheta = std::cos(l.coneAngle);
      lightPower = 2.0f * M_PI * (1.0f - cosTheta) * basePower;
    } else if (l.type == shaderio::LightType::eDirectional) { // eDirectional
      lightPower = basePower * scene.crossSectionArea;
    }

    // 3. Accumulate into the total power for the category CDF
    totalAnalyticalPower += lightPower;
  }
  return totalAnalyticalPower;
}
