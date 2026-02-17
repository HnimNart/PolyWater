#pragma once

struct LightSample {
  Vector3 L;      // The radiance (color) emitted towards the shading point
  Vector3 wi;     // The direction vector FROM the shading point TO the light
  float pdf;      // The probability density of this specific sample
  float distance; // Distance to the light (useful for shadow ray clipping)
  Vector3 normal; // The normal at the light surface (Zero for point lights)
};

class Light {
public:
  virtual ~Light() = default;

  // 1. "Sample Light Incoming": The main function for Direct Lighting
  // Given a shading point 'ref', pick a point on this light and return the
  // data.
  virtual LightSample SampleLi(const Vector3 &ref, const Vector2 &u) const = 0;

  // 2. The PDF: Given a shading point 'ref' and a direction 'wi',
  // what was the probability of picking that direction?
  virtual float PdfLi(const Vector3 &ref, const Vector3 &wi) const = 0;

  // 3. Total Power: Used by your LightManager/DiscretePDF to weight this light
  virtual float Power() const = 0;

  // 4. (Optional) Is it a Delta light? (Point/Directional)
  // Helps the renderer know if it can skip MIS for this light.
  virtual bool IsDelta() const = 0;
};
