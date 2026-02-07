#ifndef LIGHTS_H
#define LIGHTS_H 1

#include "ray.h.slang"

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
  GltfPunctual lightRaw = sceneInfo->punctualLights[lightIndex];

  // 2. Sky/Sun Override (Special Case for Index 0)
  // -----------------
  // If sky is enabled, Light 0 becomes the Sun.
  if (sceneInfo->useSky == 1 && lightIndex == 0)
  {
    result.direction = normalize(sceneInfo->skySimpleParam.sunDirection);
    result.color = sceneInfo->skySimpleParam.sunColor;
    result.intensity = sceneInfo->skySimpleParam.sunIntensity;
    result.pdf = 1.0;
    result.type = GltfLightType::eDirectional;

    // For shadow rays, we need a "target" position.
    // Since the sun is infinite, we project a point very far away.
    result.position = worldPos + result.direction * 100000.0;

    return result;
  }

  // 3. Default Setup
  // -----------------
  result.color = lightRaw.color;
  result.pdf = 1.0;  // Assuming uniform selection or single light

  // 4. Handle Light Types
  // -----------------

  // TYPE: DIRECTIONAL
  if (lightRaw.type == GltfLightType::eDirectional)
  {
    // For directional lights, position is irrelevant for intensity,
    // but we need a virtual position for the shadow ray distance check.
    result.direction = normalize(lightRaw.direction);
    result.intensity = lightRaw.intensity;
    result.position = worldPos + result.direction * 100000.0;
    result.type = GltfLightType::eDirectional;
  }
  // TYPE: POINT & SPOT
  else
  {
    // Calculate Vector L (Surface -> Light)
    float3 toLight = lightRaw.position - worldPos;
    float distSq = dot(toLight, toLight);
    float dist = sqrt(distSq);
    result.type = GltfLightType::ePoint;

    result.position = lightRaw.position;
    result.direction = toLight / dist;  // Normalize

    // A. Distance Attenuation (Inverse Square Law)
    // Avoid division by zero with max(..., 0.001)
    float attenuation = 1.0 / max(0.01, distSq);

    // B. Spot Attenuation
    if (lightRaw.type == GltfLightType::eSpot)
    {
      result.type = GltfLightType::eSpot;
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

//-----------------------------------------------------------------------
// HELPER: EVALUATE INCIDENT LIGHT (Geometric Term Only)
// Calculates the energy hitting the surface (Irradiance)
// Formula: Li * (N dot L)
//-----------------------------------------------------------------------
float3 evaluateLightGeometric(
    LightSample light,  // The light data (intensity includes 1/d^2)
    float3 N            // Surface Normal
)
{
  // 1. Geometric Term (Lambert's Cosine Law)
  // Light hitting at a grazing angle spreads out and is weaker.
  float nDotL = max(0.0, dot(N, light.direction));

  // 2. Final Irradiance
  // We strictly return the light energy arriving at the surface.
  // We do NOT multiply by Albedo or BRDF here.
  return light.color * light.intensity * nDotL;
}

//-----------------------------------------------------------------------
// DIRECT LIGHTING LOOP
//-----------------------------------------------------------------------

#ifdef RAY_TRACE
interface IBSDF
{
  float3 eval(float3 lightDir);
}

float3 sampleDirectLighting(SceneInfo* sceneInfo, float3 worldPos,
                            float3 worldNormal, IBSDF bsdf)
{
  float3 color = float3(0);
  // Loop over all lights
  for (int i = 0; i < sceneInfo->numLights; i++)
  {
    // A. Sample the Light
    LightSample light = sampleLight(sceneInfo, worldPos, i);

    // B. Geometric Check (Backface Culling)
    // Ensure light is in front of the surface
    if (dot(worldNormal, light.direction) > 0.0)
    {
      // C. Shadow Test
      float distToLight = length(light.position - worldPos);
      float visibility =
          testShadow(worldPos, worldNormal, light.direction, distToLight);

      // D. If Visible, Accumulate
      if (visibility > 0.0)
      {
        float3 materialResponse = bsdf.eval(light.direction);
        float3 irradiance = evaluateLightGeometric(light, worldNormal);
        color += materialResponse * irradiance * visibility;
      }
    }
  }
  return color;
}
#endif

//-----------------------------------------------------------------------
// LIGHT PROCESSING - Handles sky override, distance attenuation, and spot light
// cone effects
//-----------------------------------------------------------------------
// Processes light properties based on type and applies appropriate attenuation
GltfPunctual
processLight(SceneInfo sceneInfo, float3 worldPos)
{
  // Start with the first punctual light in the scene (simplified for tutorial)
  GltfPunctual light = sceneInfo.punctualLights[0];

  // Sky override: Replace punctual light with sun parameters from sky system
  // This allows using procedural sky lighting instead of manual light setup
  if (sceneInfo.useSky == 1)
  {
    light.direction =
        sceneInfo.skySimpleParam.sunDirection;        // Sun direction from sky
    light.color = sceneInfo.skySimpleParam.sunColor;  // Sun color from sky
    light.intensity =
        sceneInfo.skySimpleParam.sunIntensity;  // Sun intensity from sky
    light.type = GltfLightType::eDirectional;   // Sun is always directional
  }

  // Point light: Calculate direction from light to surface and apply distance
  // attenuation
  if (light.type == GltfLightType::ePoint)
  {
    // Direction from surface point to light position
    light.direction = light.position - worldPos;
    float d = length(light.direction);
    // Apply inverse square law: intensity decreases with distance squared
    light.intensity /= (d * d);
  }
  // Spot light: Calculate direction, apply distance attenuation and cone
  // falloff
  else if (light.type == GltfLightType::eSpot)
  {
    // Direction from surface point to light position
    float3 lightDir = light.position - worldPos;
    float d = length(lightDir);
    // Apply distance attenuation (inverse square law)
    light.intensity /= (d * d);

    // Calculate cone attenuation based on angle from spot light direction
    // theta: cosine of angle between light direction and spot direction
    float theta = dot(normalize(lightDir), normalize(light.direction));
    // Smooth falloff from inner cone (1.0) to outer cone (0.0)
    float spotIntensity =
        clamp((theta - cos(light.coneAngle)) / (1.0 - cos(light.coneAngle)),
              0.0, 1.0);
    light.intensity *= spotIntensity;
    light.direction = lightDir;
  }

  return light;
}

#endif
