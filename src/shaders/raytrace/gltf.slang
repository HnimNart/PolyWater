#include "shared.h.slang"

//-----------------------------------------------------------------------
// CLOSEST HIT SHADER - Called when ray hits geometry (performs shading)
//-----------------------------------------------------------------------
[shader("closesthit")] void
rchit_gltf(inout HitPayload payload,
           in BuiltInTriangleIntersectionAttributes attr)
{
  // Convert built-in barycentric coordinates to full barycentric coordinates
  // Built-in provides (u,v) where u+v+w=1, so w = 1-u-v
  float3 barycentrics = float3(1 - attr.barycentrics.x - attr.barycentrics.y,
                               attr.barycentrics.x, attr.barycentrics.y);

  // Get instance and primitive information from ray tracing built-ins
  uint instanceID = InstanceIndex();  // Which instance was hit
  uint meshID = InstanceID();  // Instance ID (same as instanceID in this case)
  uint triID = PrimitiveIndex();  // Which triangle within the mesh was hit

  // Retrieve scene data structures
  SceneInfo sceneInfo = pushConst.sceneInfoAddress[0];
  SceneResources sceneResources = pushConst.resourcesAddress[0];
  Instance instance = sceneResources.instances[instanceID];
  MeshPrimitive mesh = sceneResources.meshes[instance.meshIndex];
  Material material = sceneResources.materials[instance.materialIndex];

  // Interpolate vertex attributes across the hit triangle
  int3 indices = getTriangleIndices(mesh.gltfBuffer, mesh.triMesh, triID);
  float3 pos = getTriangleAttribute<float3>(
      mesh.gltfBuffer, mesh.triMesh.positions, indices, barycentrics);
  float3 nrm = getTriangleAttribute<float3>(
      mesh.gltfBuffer, mesh.triMesh.normals, indices, barycentrics);
  float2 worldTexCoord = getTriangleAttribute<float2>(
      mesh.gltfBuffer, mesh.triMesh.texCoords, indices, barycentrics);

  // Transform from object space to world space
  float3 worldPos = float3(mul(float4(pos, 1.0), ObjectToWorld4x3()));
  float3 worldNormal = normalize(mul(WorldToObject4x3(), nrm).xyz);

  // Extract material properties (PBR parameters)
  float3 albedo = material.baseColorFactor.xyz;  // Base color/albedo
  float metallic =
      material.metallicFactor;  // Metallic factor (0=dielectric, 1=metal)
  float roughness =
      material.roughnessFactor;  // Surface roughness (0=smooth, 1=rough)

  // Sample albedo texture if available
  if (material.baseColorTextureIndex > 0)
  {
    albedo *= textures[material.baseColorTextureIndex]
                  .SampleLevel(worldTexCoord, 0)
                  .xyz;
  }

  // Apply material overrides from push constants (for
  // debugging/experimentation)
  if (pushConst.metallicRoughnessOverride.x >= 0.0)
    metallic = pushConst.metallicRoughnessOverride.x;
  if (pushConst.metallicRoughnessOverride.y >= 0.0)
    roughness = pushConst.metallicRoughnessOverride.y;

  // Process light with sky override and distance attenuation
  GltfPunctual light = processLight(sceneInfo, worldPos);

  // Calculate lighting vectors
  float3 N = normalize(worldNormal);      // Surface normal
  float3 V = -WorldRayDirection();        // View direction (towards camera)
  float3 L = normalize(light.direction);  // Light direction

  // Test for shadows by casting a shadow ray towards the light
  float shadowFactor = testShadow(worldPos, N, light.direction, light);

  // Calculate PBR lighting using the metallic-roughness model
  // This computes the final color based on material properties and lighting
  float3 color = pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);

  // Apply light color, intensity, and shadow factor
  color *= light.color * light.intensity * shadowFactor;

  // Set the final color in the payload
  payload.color = color;
}
