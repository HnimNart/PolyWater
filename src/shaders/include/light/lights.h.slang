#ifndef LIGHTS_H
#define LIGHTS_H 1

#include "envmap.h.slang"
#include "include/bsdf/ibsdf.h.slang"
#include "include/core/math.h.slang"

struct LightSample {
  float3 position;
  float intensity;
  float3 direction;
  float pdf;
  int type;
  float3 color;
};

/**********************************************************/
uint selectLightIndex(float randomValue, float *cdfBuffer, uint count)
/**********************************************************/
{
  uint first = 0;
  uint len = count;

  while (len > 0) {
    uint half = len >> 1;
    uint middle = first + half;

    if (cdfBuffer[middle] < randomValue) {
      first = middle + 1;
      len -= half + 1;
    } else {
      len = half;
    }
  }
  return clamp(first, 0, count - 1);
}

/**********************************************************/
LightSample sampleLight(SceneInfo *sceneInfo,
                        float3 worldPos,
                        int lightIndex = 0)
/**********************************************************/
{
  LightSample result;

  PunctualLight lightRaw = sceneInfo->punctualLights[lightIndex];

  if (sceneInfo->useSky == 1 && lightIndex == 0) {
    result.direction = normalize(sceneInfo->skySimpleParam.sunDirection);
    result.color = sceneInfo->skySimpleParam.sunColor;
    result.intensity = sceneInfo->skySimpleParam.sunIntensity;
    result.pdf = 1.0;
    result.type = LightType::eDirectional;
    result.position = worldPos + result.direction * INFINITE;
    return result;
  }

  result.color = lightRaw.color;
  result.pdf = 1.0;

  if (lightRaw.type == LightType::eDirectional) {
    result.direction = normalize(lightRaw.direction);
    result.intensity = lightRaw.intensity;
    result.position = worldPos + result.direction * INFINITE;
    result.type = LightType::eDirectional;
  }
  else {
    float3 toLight = lightRaw.position - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(distSq);
    result.type = LightType::ePoint;

    result.position = lightRaw.position;
    result.direction = toLight / dist;

    float attenuation = 1.0 / max(0.01, distSq);

    if (lightRaw.type == LightType::eSpot) {
      result.type = LightType::eSpot;
      float theta = dot(result.direction, normalize(lightRaw.direction));
      float angleCos = cos(lightRaw.coneAngle);
      float spotFactor = clamp((theta - angleCos) / (1.0 - angleCos), 0.0, 1.0);
      attenuation *= spotFactor;
    }
    result.intensity = lightRaw.intensity * attenuation;
  }

  return result;
}

/**********************************************************/
LightSample sampleAreaLight(SceneInfo *sceneInfo, float3 refPoint,
                            inout int seed)
/**********************************************************/
{
  LightSample ls;

  ls.pdf = 0.0f;
  ls.intensity = 0.0f;
  ls.type = 3;
  ls.color = float3(0, 0, 0);
  ls.direction = float3(0, 1, 0);
  ls.position = float3(0, 0, 0);

  AreaLight areaLight = sceneInfo->areaLight;
  if (areaLight.nTriangles == 0)
    return ls;
  DevicePtr<TriangleLight> areaLightTriangles = areaLight.triangles;
  DevicePtr<float> areaLightCdf = areaLight.cdf;

  uint index = selectLightIndex(rnd(seed), areaLightCdf.get(), areaLight.nTriangles);
  TriangleLight tri = areaLightTriangles.readAt(index);

  float prevCdf = (index == 0) ? 0.0f : areaLightCdf.readAt(index - 1);
  float currentCdf = areaLightCdf.readAt(index);
  float triSelectionPdf = currentCdf - prevCdf;

  float sqrt_r1 = sqrt(rnd(seed));
  float u = 1.0f - sqrt_r1;
  float v = rnd(seed) * sqrt_r1;

  float3 p0 = tri.v0;
  float3 p1 = tri.v1;
  float3 p2 = tri.v2;
  ls.position = (1.0f - u - v) * p0 + u * p1 + v * p2;

  float3 distVec = ls.position - refPoint;
  float distSq = dot(distVec, distVec);
  float dist = sqrt(distSq);
  ls.direction = distVec / dist;

  float3 triNormal = normalize(cross(p1 - p0, p2 - p0));
  float cosThetaPrime = dot(triNormal, -ls.direction);

  if (cosThetaPrime > 1e-6f) {
    ls.color = tri.emission;
    ls.intensity = 1.0f;

    float areaPdf = 1.0f / tri.area;
    float geomTerm = distSq / cosThetaPrime;

    ls.pdf = triSelectionPdf * (areaPdf * geomTerm);
  }

  return ls;
}

#ifdef RAY_TRACE
#include "include/raytrace/trace.h.slang"

struct LightRaySample {
  float3 light;
  bool valid;
}

LightRaySample
/**********************************************************/
sampleDirectLightingPowerCDF(SceneInfo *sceneInfo, float3 worldPos,
                             float3 worldNormal, IBSDF bsdf, inout int seed)
/**********************************************************/
{
  LightRaySample sample;
  sample.light = float3(0.0f);
  sample.valid = false;
  float3 directLighting = float3(0.0f);

  EnvmapLight envLight = sceneInfo->envmapLight;
  AreaLight areaLight = sceneInfo->areaLight;

  float wAnalytic = sceneInfo->totalAnalyticalPower;
  float wArea = areaLight.totalSum;
  float wEnv = envLight.totalSum * sceneInfo->useEnv;

  float totalWeight = wAnalytic + wArea + wEnv;

  if (totalWeight <= 0.0f)
    return sample;

  float pAnalytic = wAnalytic / totalWeight;
  float pArea = wArea / totalWeight;
  float pEnv = wEnv / totalWeight;

  float cdf0 = pAnalytic;
  float cdf1 = cdf0 + pArea;

  float xi = rnd(seed);

  int selectedStrategy = -1;
  float strategyPdf = 0.0f;

  if (xi < cdf0 && pAnalytic > 0.0f) {
    selectedStrategy = 0;
    strategyPdf = pAnalytic;
  } else if (xi < cdf1 && pArea > 0.0f) {
    selectedStrategy = 1;
    strategyPdf = pArea;
  } else if (pEnv > 0.0f) {
    selectedStrategy = 2;
    strategyPdf = pEnv;
  }

  if (selectedStrategy == -1)
    return sample;

  LightSample ls;
  bool validSample = false;

  if (selectedStrategy == 0)
  {
    int lightIdx = clamp(int(rnd(seed) * sceneInfo->numLights), 0,
                         sceneInfo->numLights - 1);
    ls = sampleLight(sceneInfo, worldPos, lightIdx);

    ls.pdf *= (1.0f / float(sceneInfo->numLights));
    if (ls.intensity > 0.0f)
      validSample = true;
  } else if (selectedStrategy == 1)
  {
    ls = sampleAreaLight(sceneInfo, worldPos, seed);
    if (ls.pdf > 0.0f)
      validSample = true;
  } else if (selectedStrategy == 2)
  {

    EnvmapSampler envSampler;
    float2 randomSample = float2(rnd(seed), rnd(seed));

    float3 wi;
    float pdf;
    float3 radiance;

    envSampler.sample(envLight, randomSample, wi, pdf, radiance, textures);

    if (pdf > 0.0f) {
      ls.direction = wi;
      ls.color = radiance;
      ls.intensity = 1.0f;
      ls.pdf = pdf;
      ls.position = worldPos + wi * 1e7f;
      validSample = true;
    }
  }

  if (validSample) {
    float nDotL = dot(worldNormal, ls.direction);

    if (nDotL > 0.0f && ls.pdf > 0.0f) {
      float distToLight = length(ls.position - worldPos);
      float visibility =
          testShadow(worldPos, ls.direction, distToLight - 0.001f);

      if (visibility > 0.0f) {
        float3 brdf = bsdf.eval(ls.direction, worldNormal);

        float combinedPdf = ls.pdf * strategyPdf;

        sample.light =
            (brdf * ls.color * ls.intensity * nDotL * visibility) / combinedPdf;
      }
    }
  }

  sample.valid = true;
  return sample;
}

/**********************************************************/
float3 sampleDirectLighting(SceneInfo *sceneInfo, float3 worldPos,
                            float3 worldNormal, IBSDF bsdf)
/**********************************************************/
{
  float3 color = float3(0);
  for (int i = 0; i < sceneInfo->numLights; i++) {
    LightSample light = sampleLight(sceneInfo, worldPos, i);

    if (dot(worldNormal, light.direction) > 0.0) {
      float distToLight = length(light.position - worldPos);
      float visibility = testShadow(worldPos, light.direction, distToLight);

      if (visibility > 0.0) {
        float3 materialResponse = bsdf.eval(light.direction, worldNormal);
        float3 irradiance = light.color * light.intensity;
        color += materialResponse * irradiance * visibility;
      }
    }
  }
  return color;
}

/**********************************************************/
float3 sampleAreaLight(SceneInfo *sceneInfo, float3 worldPos,
                       float3 worldNormal, IBSDF bsdf, inout int seed)
/**********************************************************/
{

  float3 directLighting = float3(0.0f);
  if (sceneInfo->areaLight.nTriangles > 0) {
    LightSample areaLight = sampleAreaLight(sceneInfo, worldPos, seed);

    float nDotL = dot(worldNormal, areaLight.direction);

    if (nDotL > 0.0 && areaLight.pdf > 0.0) {
      float visibility =
          testShadow(worldPos, areaLight.direction,
                     length(areaLight.position - worldPos) - 0.001f);

      if (visibility > 0.0) {
        float3 brdf = bsdf.eval(areaLight.direction, worldNormal);

        float3 contribution =
            (brdf * areaLight.color * nDotL * visibility) / areaLight.pdf;

        directLighting += contribution;
      }
    }
  }

  return directLighting;
}

#endif
#endif
