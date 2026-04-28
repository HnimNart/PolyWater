#include "PointLight.hpp"

LightSample PointLight::SampleLi(const glm::vec3& ref, const glm::vec2& u) const
{
  LightSample ls;
  ls.wi = normalize(position - ref);
  ls.distance = length(position - ref);
  ls.normal = glm::vec3(0);                        // No surface normal
  ls.L = intensity / (ls.distance * ls.distance);  // Squared falloff
  ls.pdf = 1.0f;                                   // Delta distribution
  return ls;
}

float PointLight::PdfLi(const glm::vec3& ref, const glm::vec3& wi) const
{
  return 0.0f;
}

float PointLight::Power() const
{
  return 4.0f * M_PI * shaderio::bt709Luminance(intensity);
}

bool PointLight::IsDelta() const
{
  return true;
}
