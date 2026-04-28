#pragma once

#include "ILights.hpp"
#include "shaders/shared/tonemapper_io.h.slang"

class PointLight : public Light
{
  glm::vec3 position;
  glm::vec3 intensity;  // Watts/Steradian

  LightSample SampleLi(const glm::vec3& ref, const glm::vec2& u) const override;

  float PdfLi(const glm::vec3& ref, const glm::vec3& wi) const override;
  float Power() const override;
  bool IsDelta() const override;
};
