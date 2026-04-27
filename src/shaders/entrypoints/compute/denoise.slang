// ========================================================================
// denoise.slang
// ========================================================================
#include "shared/structs.h" 

// Input: Noisy HDR
[[vk::binding(0, 0)]] Texture2D<float4> inImage;

// Output: Clean HDR
[[vk::binding(1, 0)]] RWTexture2D<float4> outImage;

// Use your global push constant definition
[[vk::push_constant]]
ConstantBuffer<PushConstant> pc;

/**********************************************************/
float getLuminance(float3 color)
/**********************************************************/
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

/**********************************************************/
[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadID : SV_DispatchThreadID)
/**********************************************************/
{
    int2 pixelCoord = int2(dispatchThreadID.xy);

    uint width, height;
    inImage.GetDimensions(width, height);

    if (pixelCoord.x >= width || pixelCoord.y >= height)
        return;

    // Read the noisy color
    float4 centerColor4 = inImage[pixelCoord];
    
    // If denoise is toggled off in UI, just copy the noisy pixel and exit!
    if (pc.renderParams.denoise == 0) {
        outImage[pixelCoord] = centerColor4;
        return;
    }

    float3 centerColor = centerColor4.rgb;
    float centerLuminance = getLuminance(centerColor);

    // Read parameters from your global struct
    int blurRadius = (int)pc.renderParams.denoiseRadius;
    float spatialSigmaSq2 = 2.0 * pc.renderParams.denoiseSpatialSigma * pc.renderParams.denoiseSpatialSigma;
    float luminanceSigmaSq2 = 2.0 * pc.renderParams.denoiseLuminanceSigma * pc.renderParams.denoiseLuminanceSigma;

    spatialSigmaSq2 = max(spatialSigmaSq2, 0.0001);
    luminanceSigmaSq2 = max(luminanceSigmaSq2, 0.0001);

    float3 finalColor = float3(0.0);
    float totalWeight = 0.0;

    for (int y = -blurRadius; y <= blurRadius; ++y)
    {
        for (int x = -blurRadius; x <= blurRadius; ++x)
        {
            int2 sampleCoord = clamp(pixelCoord + int2(x, y), int2(0, 0), int2(width - 1, height - 1));

            float3 sampleColor = inImage[sampleCoord].rgb;
            float sampleLuminance = getLuminance(sampleColor);

            float spatialDistSq = (x * x + y * y);
            float spatialWeight = exp(-spatialDistSq / spatialSigmaSq2);

            float luminanceDiff = centerLuminance - sampleLuminance;
            float luminanceWeight = exp(-(luminanceDiff * luminanceDiff) / luminanceSigmaSq2);

            float weight = spatialWeight * luminanceWeight;

            finalColor += sampleColor * weight;
            totalWeight += weight;
        }
    }

    finalColor /= totalWeight;
    outImage[pixelCoord] = float4(finalColor, centerColor4.a);
}

