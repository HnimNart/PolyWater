#pragma once

#include "ILights.hpp"
#include "shaders/include/tonemapper_io.h.slang"

class AreaLight : public Light
{
  Triangle* tri;  // Reference to the actual geometry
  glm::vec3 emission;

  LightSample SampleLi(const glm::vec3& ref, const glm::vec2& u) const override;

  float Power() const override;
  bool IsDelta() const override;
};
