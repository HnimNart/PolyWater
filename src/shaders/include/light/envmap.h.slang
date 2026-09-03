#pragma once

#include "shared/tonemapper_io.h.slang"

// Helper: Binary search to find the lower bound index in a float array
/**********************************************************/
int findLowerBound(float *cdf, int size, float u)
/**********************************************************/
{
  int low = 0;
  int high = size - 1;
  while (low < high) {
    int mid = (low + high) / 2;
    if (cdf[mid] < u)
      low = mid + 1;
    else
      high = mid;
  }
  return max(0, low - 1);
}

struct EnvmapSampler {
  // 1. Pick a direction based on two random numbers [0, 1)
  void sample(EnvmapLight light, float2 randomSample, out float3 wi,
              out float pdf, out float3 radiance, Sampler2D envTextures[])
  {

    // A. Find Row (V) using Marginal CDF
    int vIdx =
        findLowerBound(light.cdfCols.get(), light.dims.y + 1, randomSample.y);
    float v = (float(vIdx) + 0.5f) / float(light.dims.y);

    // B. Find Column (U) using Conditional CDF
    float *rowCDF = light.cdfRows.at((vIdx * (light.dims.x + 1)));
    int uIdx = findLowerBound(rowCDF, light.dims.x + 1, randomSample.x);
    float u = (float(uIdx) + 0.5f) / float(light.dims.x);

    // C. Convert UV to Spherical Coordinates (Y-up)
    float phi = u * 2.0f * M_PI;
    float theta = v * M_PI;

    float sinTheta = sin(theta);
    float cosTheta = cos(theta);

    float3 localDir =
        float3(sinTheta * sin(phi), cosTheta, sinTheta * cos(phi));

    // D. Transform to World Space
    wi = normalize(mul((float3x3)light.rotation, localDir));

    // E. Evaluate Radiance and PDF
    radiance = evalRadiance(light, wi, envTextures);
    pdf = evalPdf(light, wi, radiance);
  }

  // 2. Evaluate radiance for a given world-space direction
  float3 evalRadiance(EnvmapLight light, float3 wi, Sampler2D envTextures[])
  {
    float3 localDir = normalize(mul((float3x3)transpose(light.rotation), wi));

    float theta = acos(clamp(localDir.y, -1.0f, 1.0f));
    float phi = atan2(localDir.x, localDir.z);

    if (phi < 0.0f)
      phi += 2.0f * M_PI;

    float u = phi / (2.0f * M_PI);
    float v = theta / M_PI;

    // Using your Sampler2D interface
    float3 color =
        envTextures[light.envTextureIdx].SampleLevel(float2(u, v), 0).rgb;

    return color * light.scale;
  }

  // 3. Evaluate PDF for MIS
  float evalPdf(EnvmapLight light, float3 wi, float3 radiance)
  {

    uint width = light.dims.x;
    uint height = light.dims.y;

    float3 localDir = normalize(mul((float3x3)transpose(light.rotation), wi));

    float theta = acos(clamp(localDir.y, -1.0f, 1.0f));
    float sinTheta = sin(theta);

    // Handle the singularity at the poles
    if (sinTheta <= 1e-6f)
      return 0.0f;

    // Importance is luminance (scale is cancelled out in the ratio)
    float luminance = bt709Luminance(radiance) / light.scale;

    // Standard PDF for Equirectangular Importance Sampling:
    // pdf = luminance / (totalIntegral * 2 * PI^2)
    // We don't divide by sinTheta here because the sampling logic
    // already accounts for the solid angle Jacobian.
    return (luminance * width * height) /
           (light.totalSum * (2.0f * M_PI * M_PI));
  }
};
