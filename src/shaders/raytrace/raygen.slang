#include "shared.h.slang"

[shader("raygeneration")] void rgenMain()
{
  // --- CONFIG ---
  RenderParams params = pushConst.renderParams;
  const int N_SAMPLES = params.nSamples;      // Anti-Aliasing quality
  const int MAX_BOUNCES = params.maxBounces;  // How many times light bounces

  // --- SETUP ---
  uint2 launchID = DispatchRaysIndex().xy;
  float2 launchSize = (float2) DispatchRaysDimensions().xy;
  SceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  float3 finalPixelColor = float3(0, 0, 0);

  // Initialize RNG
  uint seed =
      tea(launchID.y * (uint) launchSize.x + launchID.x, params.frameIdx);

  // --- OUTER LOOP: SAMPLES (Anti-Aliasing) ---
  for (int s = 0; s < N_SAMPLES; s++)
  {
    // 1. Jitter & Camera Ray (Same as before)
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

    // 2. INIT RAY & PAYLOAD
    const uint rayFlags = 0;
    const uint rayMask = 0xFF;

    RayDesc ray;
    ray.Origin = origin.xyz;
    ray.Direction = normalize(direction.xyz);
    ray.TMin = EPSILON;
    ray.TMax = INFINITE;

    HitPayload payload;
    payload.color = float3(0, 0, 0);   // Accumulator for this sample
    payload.weight = float3(1, 1, 1);  // Starts at 100% throughput
    payload.depth = 0;
    payload.seed = sampleSeed;
    payload.stop = true;
    payload.emit = true;

    // --- INNER LOOP: BOUNCES (Path Tracing) ---
    for (int bounce = 0; bounce < MAX_BOUNCES; bounce++)
    {
      // A. Trace
      // The Hit Shader will update 'color' (emissive) and 'weight'
      // (albedo) It will also set 'nextRayOrigin' and 'nextRayDir'
      TraceRay(topLevelAS, rayFlags, rayMask, 0, 0, 0, ray, payload);

      // B. Check Termination
      // Did we miss everything? Or hit a light that absorbs all bounces?
      if (payload.stop)
      {
        break;
      }

      // C. Russian Roulette (Optional Optimization)
      float p = max(payload.weight.r, max(payload.weight.g, payload.weight.b));
      if (bounce > params.n_bounces_rr)
      {
        if (rnd(payload.seed) > p)
        {
          break;
        }
        payload.weight /= p;  // Boost survivors to keep math correct
      }

      // D. Prepare Next Ray
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
  }

  // --- FINALIZE ---
  outImage[int2(launchID)] = float4(finalPixelColor / float(N_SAMPLES), 1.0);
}
