#pragma once

class PointLight : public Light {
  Vector3 position;
  Vector3 intensity; // Watts/Steradian

  LightSample SampleLi(const Vector3 &ref, const Vector2 &u) const override {
    LightSample ls;
    ls.wi = normalize(position - ref);
    ls.distance = length(position - ref);
    ls.normal = Vector3(0);                         // No surface normal
    ls.L = intensity / (ls.distance * ls.distance); // Squared falloff
    ls.pdf = 1.0f;                                  // Delta distribution
    return ls;
  }

  float PdfLi(const Vector3 &ref, const Vector3 &wi) const override {
    return 0.0f;
  }
  float Power() const override { return 4.0f * PI * luminance(intensity); }
  bool IsDelta() const override { return true; }
};
