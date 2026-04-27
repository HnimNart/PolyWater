#pragma once

#include "ILights.hpp"
#include "shaders/shared/tonemapper_io.h.slang"

class PointLight : public Light
{
  glm::vec3 position;
  glm::vec3 intensity;  // Watts/Steradian

  LightSample SampleLi(const glm::vec3& ref, const glm::vec2& u) const override
  {
    LightSample ls;
    ls.wi = normalize(position - ref);
    ls.distance = length(position - ref);
    ls.normal = glm::vec3(0);                        // No surface normal
    ls.L = intensity / (ls.distance * ls.distance);  // Squared falloff
    ls.pdf = 1.0f;                                   // Delta distribution
    return ls;
  }

  float PdfLi(const glm::vec3& ref, const glm::vec3& wi) const override
  {
    return 0.0f;
  }
  float Power() const override
  {
    return 4.0f * M_PI * shaderio::bt709Luminance(intensity);
  }
  bool IsDelta() const override { return true; }
};
