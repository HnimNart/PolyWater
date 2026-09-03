/*
 * Copyright (c) 2022-2025, NVIDIA CORPORATION.  All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION.
 * SPDX-License-Identifier: Apache-2.0
 */


#include "shared/tonemapper_io.h.slang"
#include "include/core/functions.h.slang"

layout(push_constant) ConstantBuffer<TonemapperData> tm;

layout(binding = TonemapBinding::eImageInput) Texture2D<float4> InColorBuffer;
layout(binding = TonemapBinding::eImageOutput) RWTexture2D<float4> outImage;
layout(binding = TonemapBinding::eHistogramInputOutput) RWStructuredBuffer<uint> InOutHistogram;
layout(binding = TonemapBinding::eLuminanceInputOutput) RWStructuredBuffer<float> OutLuminance;

groupshared float g_localFloatData[EXPOSURE_HISTOGRAM_SIZE];
groupshared uint  g_localUintData[EXPOSURE_HISTOGRAM_SIZE];

//////////////////////////////////////////////////////////////////////////////
// Tonemapping
/////////////////////////////////////////////////////////////////////////////

/**********************************************************/
[shader("compute")]
[numthreads(TONEMAP_WORKGROUP_SIZE, TONEMAP_WORKGROUP_SIZE, 1)]
void Tonemap(uint2 globalThreadID: SV_DispatchThreadID)
/**********************************************************/
{
  uint2 imageSize;
  outImage.GetDimensions(imageSize.x, imageSize.y);

  if(any(globalThreadID >= imageSize))
    return;

  float4 color = InColorBuffer[int2(globalThreadID.xy)];

  if(tm.isActive == 1)
  {
    if(tm.autoExposure == 1)
    {
      const float middleGrey         = 0.18f;
      float       exposureMultiplier = middleGrey / max(0.001f, OutLuminance[0]);
      color.xyz *= exposureMultiplier;
    }

    color.xyz = applyTonemap(tm, color.xyz, float2(globalThreadID.xy), float2(imageSize));
  }

  outImage[int2(globalThreadID.xy)] = color;
}


//////////////////////////////////////////////////////////////////////////////
// Histogram Tonemapping
/////////////////////////////////////////////////////////////////////////////

/**********************************************************/
uint inputToHistogramBucket(const float3 inputColor)
/**********************************************************/
{
  const float inputLuminance = bt709Luminance(inputColor);

  const float kEpsilon = 0.00001f;
  if(inputLuminance < kEpsilon)
  {
    return 0;
  }

  const float ev100        = luminanceEv100(inputLuminance);
  const float evRange      = tm.evMaxValue - tm.evMinValue;
  const float normalizedEV = saturate((ev100 - tm.evMinValue) / evRange);

  return uint(normalizedEVToHistogramOffset(normalizedEV));
}

/**********************************************************/
float centerMeteringWeight(float2 screenUV, float aspect)
/**********************************************************/
{
  if(tm.enableCenterMetering == 0)
  {
    return 1.f;
  }

  const float weight = saturate(length((screenUV - float2(0.5f)) / float2(1.0f, aspect)) / tm.centerMeteringSize);
  return lerp(1.0f, 0.0f, weight);
}

/**********************************************************/
[shader("compute")]
[numthreads(TONEMAP_WORKGROUP_SIZE, TONEMAP_WORKGROUP_SIZE, 1)]
void Histogram(uint2 threadId: SV_DispatchThreadID, uint linearIndex: SV_GroupIndex)
/**********************************************************/
{
  g_localUintData[linearIndex] = 0;

  GroupMemoryBarrierWithGroupSync();

  uint2 dimensions;
  InColorBuffer.GetDimensions(dimensions.x, dimensions.y);

  if(all(threadId < dimensions))
  {
    const float3 inputColor = InColorBuffer[threadId].xyz;

    const float weight = centerMeteringWeight(float2(threadId) / dimensions, (float)dimensions.x / dimensions.y);

    const uint bucketIdx = inputToHistogramBucket(inputColor);

    InterlockedAdd(g_localUintData[bucketIdx], uint(weight * 255.0f));
  }

  GroupMemoryBarrierWithGroupSync();

  uint localBinValue = g_localUintData[linearIndex];
  if(localBinValue != 0)
  {
    InterlockedAdd(InOutHistogram[linearIndex], localBinValue);
  }
}


/////////////////////////////////////////////////////////////////////////////////////////
// Auto Exposure
/////////////////////////////////////////////////////////////////////////////////////////

/**********************************************************/
float normalizedEVToHistogramOffset(float normalizedEV)
/**********************************************************/
{
  return (saturate(normalizedEV) * float(EXPOSURE_HISTOGRAM_SIZE - 2)) + 1;
}

// Inclusive parallel prefix sum across all threads in the group.
// Uses wave-level prefix sum within waves, then hierarchically combines results.
/**********************************************************/
float computePrefixSum(float val, const uint linearIndex)
/**********************************************************/
{
  GroupMemoryBarrierWithGroupSync();

  val = WavePrefixSum(val) + val;
  g_localFloatData[linearIndex] = val;
  GroupMemoryBarrierWithGroupSync();

  for(uint k = WaveGetLaneCount(); k < EXPOSURE_HISTOGRAM_SIZE; k *= 2)
  {
    uint blockIndex = linearIndex / k;
    if((blockIndex % 2) == 1)
      g_localFloatData[linearIndex] += g_localFloatData[k * blockIndex - 1];

    GroupMemoryBarrierWithGroupSync();
  }

  return g_localFloatData[EXPOSURE_HISTOGRAM_SIZE - 1];
}

/**********************************************************/
float getHistogramWeight(uint index)
/**********************************************************/
{
  return 1.0;
  // float weightUV = (index + 0.5) / float(EXPOSURE_HISTOGRAM_SIZE);
  // return InExposureCompensation.SampleLevel(weightUV, 0).r;
}

/**********************************************************/
float histogramOffsetToNormalizedEV(float offset)
/**********************************************************/
{
  return saturate((offset - 1) / float(EXPOSURE_HISTOGRAM_SIZE - 2));
}

/**********************************************************/
[shader("compute")]
[numthreads(EXPOSURE_HISTOGRAM_SIZE, 1, 1)]
void AutoExposure(uint2 threadId: SV_DispatchThreadID, uint linearIndex: SV_GroupIndex)
/**********************************************************/
{
  float weight                 = 1.0;
  float countThisBucket        = (float)InOutHistogram[linearIndex] * weight;
  float totalWeight            = computePrefixSum(countThisBucket, linearIndex);
  float adaptedCountThisBucket = countThisBucket;

  if(tm.averageMode == 0)
  {
    adaptedCountThisBucket *= linearIndex;
  }

  const float weightedSum = computePrefixSum(adaptedCountThisBucket, linearIndex);

  float normalizedEV = 0;
  if(linearIndex == 0)
  {
    uint discardSampleCount = uint(countThisBucket);

    float weightedAverage;

    if(tm.averageMode == 1)
    {
      float median    = (weightedSum - discardSampleCount) / 2.f;
      weightedAverage = 0.5f;

      for(uint i = 1; i < EXPOSURE_HISTOGRAM_SIZE; i++)
      {
        if(g_localFloatData[i] > median)
        {
          break;
        }

        weightedAverage = i + 0.5f;
      }
    }
    else
    {
      weightedAverage = weightedSum / totalWeight;
    }

    normalizedEV = histogramOffsetToNormalizedEV(weightedAverage);

    const float evRange         = tm.evMaxValue - tm.evMinValue;
    const float currentEV100    = normalizedEV * evRange + tm.evMinValue;
    const float targetLuminance = ev100Luminance(currentEV100);

    const float previousLuminance = OutLuminance[0];

    const float adaptedLuminance =
        previousLuminance + (targetLuminance - previousLuminance) * (1.f - exp2(-tm.autoExposureSpeed));

    if(linearIndex == 0)
    {
      OutLuminance[0] = adaptedLuminance;
    }
  }

  InOutHistogram[linearIndex] = 0;
}
