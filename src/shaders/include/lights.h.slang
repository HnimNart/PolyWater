#ifndef LIGHTS_H
#define LIGHTS_H 1

//-----------------------------------------------------------------------
// LIGHT PROCESSING - Handles sky override, distance attenuation, and spot light
// cone effects
//-----------------------------------------------------------------------
// Processes light properties based on type and applies appropriate attenuation
GltfPunctual processLight(SceneInfo sceneInfo, float3 worldPos)
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
