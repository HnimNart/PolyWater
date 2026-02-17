#ifndef LIGHTS_H
#define LIGHTS_H 1

#include "include/bsdf/ibsdf.h.slang"
#include "include/core/math.h.slang"

//-----------------------------------------------------------------------
// STRUCTS
//-----------------------------------------------------------------------
struct LightSample
{
  float3 position;  // World position of the light (or virtual position for Sun)
  float intensity;  // Final attenuated intensity (Radiance factor)
  float3 direction;  // Normalized Vector FROM Surface TO Light (L)
  float pdf;     // Probability of picking this light (usually 1.0 / numLights)
  int type;      // Type of the light (0 = point, 1 = spot, 2 = directional)
  float3 color;  // Light Color (RGB)
};

uint selectLightIndex(float randomValue, float* cdfBuffer, uint count)
{
    uint first = 0;
    uint len = count;

    while (len > 0)
    {
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

//-----------------------------------------------------------------------
// HELPER: LIGHT SAMPLING FUNCTION
//-----------------------------------------------------------------------
LightSample sampleLight(SceneInfo* sceneInfo,  // Pointer to scene data
                        float3 worldPos,       // Surface position being shaded
                        int lightIndex = 0)    // Index of light to sample
{
  LightSample result;

  // 1. Fetch Raw Data
  // -----------------
  PunctualLight lightRaw = sceneInfo->punctualLights[lightIndex];

  // 2. Sky/Sun Override (Special Case for Index 0)
  // -----------------
  // If sky is enabled, Light 0 becomes the Sun.
  if (sceneInfo->useSky == 1 && lightIndex == 0)
  {
    result.direction = normalize(sceneInfo->skySimpleParam.sunDirection);
    result.color = sceneInfo->skySimpleParam.sunColor;
    result.intensity = sceneInfo->skySimpleParam.sunIntensity;
    result.pdf = 1.0;
    result.type = LightType::eDirectional;

    // For shadow rays, we need a "target" position.
    // Since the sun is infinite, we project a point very far away.
    result.position = worldPos + result.direction * INFINITE;

    return result;
  }

  // 3. Default Setup
  // -----------------
  result.color = lightRaw.color;
  result.pdf = 1.0;  // Assuming uniform selection or single light

  // 4. Handle Light Types
  // -----------------

  // TYPE: DIRECTIONAL
  if (lightRaw.type == LightType::eDirectional)
  {
    // For directional lights, position is irrelevant for intensity,
    // but we need a virtual position for the shadow ray distance check.
    result.direction = normalize(lightRaw.direction);
    result.intensity = lightRaw.intensity;
    result.position = worldPos + result.direction * INFINITE;
    result.type = LightType::eDirectional;
  }
  // TYPE: POINT & SPOT
  else
  {
    // Calculate Vector L (Surface -> Light)
    float3 toLight = lightRaw.position - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(distSq);
    result.type = LightType::ePoint;

    result.position = lightRaw.position;
    result.direction = toLight / dist;  // Normalize

    // A. Distance Attenuation (Inverse Square Law)
    // Avoid division by zero directionth max(..., 0.001)
    float attenuation = 1.0 / max(0.01, distSq);

    // B. Spot Attenuation
    if (lightRaw.type == LightType::eSpot)
    {
      result.type = LightType::eSpot;
      // Based on your previous logic:
      // Assuming lightRaw.direction is stored as Vector TO Light (aligned directionth
      // L)
      float theta = dot(result.direction, normalize(lightRaw.direction));

      float angleCos = cos(lightRaw.coneAngle);
      float spotFactor = clamp((theta - angleCos) / (1.0 - angleCos), 0.0, 1.0);

      attenuation *= spotFactor;
    }
    // Combine raw intensity directionth physical falloff
    result.intensity = lightRaw.intensity * attenuation;
  }

  return result;
}



// ------------------------------------------------------------------
// Main Sampling Function
// ------------------------------------------------------------------
LightSample sampleAreaLight(SceneInfo* sceneInfo, float3 refPoint, inout int seed)
{
    LightSample ls;
    
    // Initialize defaults (invalid sample)
    ls.pdf = 0.0f;
    ls.intensity = 0.0f;
    ls.type = 3; // Custom type for Area Light
    ls.color = float3(0, 0, 0);
    ls.direction = float3(0, 1, 0);
    ls.position = float3(0, 0, 0);


    AreaLight areaLight = sceneInfo->areaLight;
    if (areaLight.nTriangles == 0) return ls;
    TriangleLight * areaLightTriangles = areaLight.triangles;
    float * areaLightCdf = areaLight.cdf;


    // 1. Select the specific triangle using the CDF (Discrete sampling)
    uint index = selectLightIndex(rnd(seed), areaLightCdf, areaLight.nTriangles);
    TriangleLight tri = areaLightTriangles[index];

    // Calculate probability of choosing this specific triangle
    // P(tri) = CDF[i] - CDF[i-1]
    float prevCdf = (index == 0) ? 0.0f : areaLightCdf[index - 1];
    float currentCdf = areaLightCdf[index];
    float triSelectionPdf = currentCdf - prevCdf;

    // 2. Sample a random point on the triangle (Continuous sampling)
    // Uniform mapping from Square to Triangle
    float sqrt_r1 = sqrt(rnd(seed));
    float u = 1.0f - sqrt_r1;
    float v = rnd(seed) * sqrt_r1;
    
    // Barycentric interpolation
    float3 p0 = tri.v0;
    float3 p1 = tri.v1;
    float3 p2 = tri.v2;
    ls.position = (1.0f - u - v) * p0 + u * p1 + v * p2;

    // 3. Compute Light Vector and Distance
    float3 distVec = ls.position - refPoint;
    float distSq = dot(distVec, distVec);
    float dist = sqrt(distSq);
    ls.direction = distVec / dist; // Normalize

    // 4. Compute Geometry Term and PDF
    // Calculate geometric normal of the light
    float3 triNormal = normalize(cross(p1 - p0, p2 - p0));
    
    // Cosine at the light source (dot of Light Normal and Ray arriving at light)
    // Note: Ray arrives at light = -ls.direction
    float cosThetaPrime = dot(triNormal, -ls.direction);

    // If cosThetaPrime <= 0, we hit the back of the light (cull it)
    if (cosThetaPrime > 1e-6f)
    {
        ls.color = tri.emission; 
        ls.intensity = 1.0f; // Color already contains radiance, so scalar is 1.0

        // Convert Area PDF to Solid Angle PDF
        // PDF_solidAngle = PDF_area * (dist^2 / cosTheta')
        // PDF_area = 1.0 / Area
        float areaPdf = 1.0f / tri.area;
        float geomTerm = distSq / cosThetaPrime;
        
        // Final PDF = (Probability of picking triangle) * (Probability of picking point on it)
        ls.pdf = triSelectionPdf * (areaPdf * geomTerm);
    }

    return ls;
}


#ifdef RAY_TRACE
#include "include/raytrace/trace.h.slang"

float3 sampleDirectLighting(SceneInfo* sceneInfo, float3 worldPos,
                            float3 worldNormal, IBSDF bsdf)
{
  float3 color = float3(0);
  // Loop over all lights
  for (int i = 0; i < sceneInfo->numLights; i++)
  {
    LightSample light = sampleLight(sceneInfo, worldPos, i);

    if (dot(worldNormal, light.direction) > 0.0)
    {
      // C. Shadow Test
      float distToLight = length(light.position - worldPos);
      float visibility = testShadow(worldPos, light.direction, distToLight);

      // D. If Visible, Accumulate
      if (visibility > 0.0)
      {
        float3 materialResponse = bsdf.eval(light.direction, worldNormal);
        float3 irradiance = light.color * light.intensity;
        color += materialResponse * irradiance * visibility;
      }
    }
  }
  return color;
}

float3 sampleDirectLighting(SceneInfo* sceneInfo, float3 worldPos, 
                           float3 worldNormal, IBSDF bsdf, inout int seed)
{
    float3 directLighting = float3(0);

    // --- 1. Analytical Lights (Point, Spot, Directional) ---
    // We iterate through these as usual, or pick one at random.
    // for (int i = 0; i < sceneInfo->numLights; i++)
    // {
    //     LightSample light = sampleLight(sceneInfo, worldPos, i);

    //     float nDotL = dot(worldNormal, light.direction);
    //     if (nDotL > 0.0 && light.pdf > 0.0)
    //     {
    //         float visibility = testShadow(worldPos, light.direction, length(light.position - worldPos));

    //         if (visibility > 0.0)
    //         {
    //             float3 brdf = bsdf.eval(light.direction, worldNormal);
    //             // For analytical lights, PDF is usually 1.0 or handled in intensity
    //             directLighting += (brdf * light.color * light.intensity * nDotL * visibility);
    //         }
    //     }
    // }

    // --- 2. Area Lights (Importance Sampled) ---
    if (sceneInfo->areaLight.nTriangles > 0)
    {
        // Use the random numbers (rnd) to pick and sample a point on an area light
        LightSample areaLight = sampleAreaLight(sceneInfo, worldPos, seed);

        float nDotL = dot(worldNormal, areaLight.direction);
        
        // Ensure the light is above the horizon and the sample is valid (pdf > 0)
        if (nDotL > 0.0 && areaLight.pdf > 0.0)
        {
            // Note: Use a small epsilon for shadow rays to avoid self-intersection
            float visibility = testShadow(worldPos, areaLight.direction, length(areaLight.position - worldPos) - 0.001f);

            if (visibility > 0.0)
            {
                float3 brdf = bsdf.eval(areaLight.direction, worldNormal);
                
                // The Monte Carlo estimator: (f * L * cos) / pdf
                // areaLight.color is the Radiance (Le)
                float3 contribution = (brdf * areaLight.color * nDotL * visibility) / areaLight.pdf;
                
                directLighting += contribution;
            }
        }
    }

    return directLighting;
}

#endif
#endif
