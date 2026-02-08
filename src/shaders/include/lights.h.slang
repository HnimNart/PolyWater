#ifndef LIGHTS_H
#define LIGHTS_H 1

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
    // Avoid division by zero with max(..., 0.001)
    float attenuation = 1.0 / max(0.01, distSq);

    // B. Spot Attenuation
    if (lightRaw.type == LightType::eSpot)
    {
      result.type = LightType::eSpot;
      // Based on your previous logic:
      // Assuming lightRaw.direction is stored as Vector TO Light (aligned with
      // L)
      float theta = dot(result.direction, normalize(lightRaw.direction));

      float angleCos = cos(lightRaw.coneAngle);
      float spotFactor = clamp((theta - angleCos) / (1.0 - angleCos), 0.0, 1.0);

      attenuation *= spotFactor;
    }
    // Combine raw intensity with physical falloff
    result.intensity = lightRaw.intensity * attenuation;
  }

  return result;
}

#endif
