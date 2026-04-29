#include "entrypoints/raytrace/ray_common.h.slang"

/**********************************************************/
[shader("raygeneration")] void rgenMain()
/**********************************************************/
{
  // --- CONFIG ---
  RenderParams params = pushConst.renderParams;
  const int N_SAMPLES = params.nSamples;      // Anti-Aliasing quality
  const int MAX_BOUNCES = params.maxBounces;  // How many times light bounces

  // --- SETUP ---
  uint2 launchID = DispatchRaysIndex().xy;
  float2 launchSize = (float2) DispatchRaysDimensions().xy;
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress.get();

  float3 finalPixelColor = float3(0, 0, 0);
  // First-hit auxiliary data for the OIDN denoiser (captured from sample 0).
  float3 firstHitAlbedo = float3(0, 0, 0);
  float3 firstHitNormal = float3(0, 0, 0);

  // Initialize RNG
  uint seed =
      tea(launchID.y * (uint) launchSize.x + launchID.x, params.frameIdx);

  for (int s = 0; s < N_SAMPLES; s++)
  {
    // 1. Jitter & Camera Ray
    uint sampleSeed = tea(seed, s);
    float r1 = (float) (tea(sampleSeed, 1) & 0x00FFFFFF) / (float) 0x01000000;
    float r2 = (float) (tea(sampleSeed, 2) & 0x00FFFFFF) / (float) 0x01000000;
    float2 jitter = float2(r1, r2) - 0.5f;

    const float2 pixelCenter = (float2) launchID + float2(0.5, 0.5) + jitter;
    const float2 inUV = pixelCenter / launchSize;
    const float2 d = inUV * 2.0 - 1.0;

    float4 target = mul(float4(d.x, d.y, 1.0, 1.0), sceneInfo->projInvMatrix);
    float4 direction =
        mul(float4(normalize(target.xyz), 0.0), sceneInfo->viewInvMatrix);
    float4 origin = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo->viewInvMatrix);

    // INIT RAY & PAYLOAD
    const uint rayFlags = 0;
    const uint rayMask = 0xFF;

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = EPSILON;
    ray.TMax = INFINITE;

    HitPayload payload;
    payload.color = float3(0, 0, 0);   
    payload.weight = float3(1, 1, 1);  
    payload.depth = 0;
    payload.seed = sampleSeed;
    payload.stop = true;
    payload.emit = true;
    payload.albedo = float3(0, 0, 0);
    payload.normal = float3(0, 0, 0);

    // Path Tracing
    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++)
    {
      TraceRay(topLevelAS, rayFlags, rayMask, 0, 0, 0, ray, payload);
      if (payload.stop)
      {
        break;
      }

      // Russian Roulette
      float p = max(payload.weight.r, max(payload.weight.g, payload.weight.b));
      if (bounce > params.nBouncesRR)
      {
        if (rnd(payload.seed) > p)
        {
          break;
        }
        payload.weight /= p;  // Boost survivors to keep math correct
      }

      // Update the ray descriptor with data from the Hit Shader
      ray.Origin = payload.nextRayOrigin;
      ray.Direction = payload.nextRayDir;
      ray.TMin = EPSILON;
      ray.TMax = INFINITE;

      // Increment depth counter in payload just in case logic needs it
      payload.depth = bounce + 1;
    }

    // Add this sample's full path result to the pixel
    finalPixelColor += payload.color;

    // Capture first-hit auxiliary data from sample 0 at frame reset.
    // Hit shaders populate payload.albedo/normal only on depth==0, so these
    // are stable across samples for the same pixel.
    if (s == 0 && params.frameIdx == 0)
    {
      firstHitAlbedo = payload.albedo;
      firstHitNormal = payload.normal;
    }
  }

  float3 currentFrameColor = finalPixelColor / float(N_SAMPLES);

  // Write first-hit albedo and normal for the OIDN denoiser.
  // These are captured from sample 0's first bounce and written only when
  // frameIdx==0 (accumulation reset), keeping the buffers stable across
  // accumulation frames.
  if (params.frameIdx == 0)
  {
    albedoImage[int2(launchID)] = float4(firstHitAlbedo, 1.0);
    // Normals are stored in world-space in the range [-1, 1].  The G-buffer
    // uses R32G32B32A32_SFLOAT which supports negative values, so no encoding
    // is needed.  OIDN reads these directly as world-space normals.
    normalImage[int2(launchID)] = float4(firstHitNormal, 0.0);
  }

  if (params.frameIdx == 0)
  {
    accumImage[int2(launchID)] = float4(currentFrameColor, 1.0);
  }
  else
  {
    float4 previousAccum = accumImage[int2(launchID)];
    accumImage[int2(launchID)] = previousAccum + float4(currentFrameColor, 1.0);
  }

  float4 totalAccum = accumImage[int2(launchID)];
  float3 averageColor =
      totalAccum.rgb / totalAccum.a;  // .a stores the count of frames

  // Write to output image
  outImage[int2(launchID)] = float4(averageColor, 1.0F);
}
