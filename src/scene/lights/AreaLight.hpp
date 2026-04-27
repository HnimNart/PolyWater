#pragma once

#include "ILights.hpp"
#include "shaders/include/tonemapper_io.h.slang"

class AreaLight : public Light
{
  Triangle* tri;  // Reference to the actual geometry
  glm::vec3 emission;

  LightSample SampleLi(const glm::vec3& ref, const glm::vec2& u) const override
  {
    // 1. Pick a point on the triangle using barycentric coordinates
    SurfacePoint p = tri->Sample(u);

    LightSample ls;
    ls.wi = normalize(p.position - ref);
    ls.distance = length(p.position - ref);
    ls.normal = p.normal;
    ls.L = (dot(p.normal, -ls.wi) > 0) ? emission : glm::vec3(0);

    // 2. Convert Area PDF to Solid Angle PDF
    float area = tri->Area();
    float distSq = ls.distance * ls.distance;
    float cosTheta = dot(p.normal, -ls.wi);

    if (cosTheta <= 0)
      ls.pdf = 0;
    else
      ls.pdf = (1.0f / area) * (distSq / cosTheta);

    return ls;
  }

  float Power() const override
  {
    return tri->Area() * shaderio::bt709Luminance(emission) * PI;
  }
  bool IsDelta() const override { return false; }
};
