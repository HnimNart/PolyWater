#pragma once

#include "math.h.slang"

float3 hsv2rgb(float3 c)
{
    float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    float3 p = abs(frac(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * lerp(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}


float4 colorFromIndex(uint index)
{
    uint h = hash(index);
    float hue = float(h) / 4294967295.0;
    float3 rgb = hsv2rgb(float3(hue, 0.8, 1.0));
    return float4(rgb, 1.0);
}
