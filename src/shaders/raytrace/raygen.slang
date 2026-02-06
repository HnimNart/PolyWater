#include "shared.h.slang"

//-----------------------------------------------------------------------
// RAY GENERATION SHADER - Entry point for each pixel in the output image
//-----------------------------------------------------------------------
[shader("raygeneration")] void rgenMain()
{
  // Get the current pixel coordinates and image dimensions
  float2 launchID = (float2) DispatchRaysIndex().xy;  // Current pixel (x,y)
  float2 launchSize =
      (float2) DispatchRaysDimensions().xy;  // Image size (width,height)

  // Retrieve scene information from push constants
  GltfSceneInfo* sceneInfo = pushConst.sceneInfoAddress;

  // Set ray tracing flags (0 = no special flags)
  const uint rayFlags = 0;

  // Convert pixel coordinates to normalized device coordinates (NDC)
  // Range: [-1,1] for both x and y
  const float2 clipCoords = launchID / launchSize * 2.0 - 1.0;

  // Transform from NDC to view space using inverse projection matrix
  const float4 viewCoords =
      mul(float4(clipCoords, 1.0, 1.0), sceneInfo.projInvMatrix);

  // Create the primary ray
  RayDesc ray;
  // Transform camera origin (0,0,0) from view space to world space
  ray.Origin = mul(float4(0.0, 0.0, 0.0, 1.0), sceneInfo.viewInvMatrix).xyz;
  // Transform ray direction from view space to world space
  ray.Direction =
      mul(float4(normalize(viewCoords.xyz), 0.0), sceneInfo.viewInvMatrix).xyz;
  ray.TMin = 0.001;     // Minimum distance to avoid self-intersection
  ray.TMax = INFINITE;  // Maximum distance (infinite for primary rays)

  // Initialize ray payload with default values
  HitPayload payload;
  payload.color = float3(0, 0, 0);  // Start with black
  payload.weight = 1;               // Full weight for primary rays
  payload.depth = 0;                // Start at depth 0

  // Cast the ray into the scene using the acceleration structure
  // Parameters: AS, flags, instance mask, sbt offset, sbt stride, miss offset,
  // ray, payload
  TraceRay(topLevelAS, rayFlags, 0xff, 0, 0, 0, ray, payload);

  // Get the final color from the ray tracing result
  float3 color = payload.color;

  // Write the result to the output image
  outImage[int2(launchID)] = float4(color, 1.0);
}
