#include "shared.h.slang"

struct PbrEvaluator : IBSDF {
  float3 V;
  float3 albedo;
  float roughness;
  float metallic;

  // Slang Constructor (Initializer)
  __init(float3 _V, float3 _albedo, float _roughness, float _metallic) {
    this.V = _V;
    this.albedo = _albedo;
    this.roughness = _roughness;
    this.metallic = _metallic;
  }

  // The required function from IBSDF interface
  float3 eval(float3 L, float3 N) {
    // Calls your existing PBR math function
    return pbrMetallicRoughness(albedo, metallic, roughness, N, V, L);
  }

  float3 sample(float3 viewDir, float3 worldNormal, inout int seed,
                inout float3 bounceDir) {

    // Deciding where to go next (Specular vs Diffuse)
    // A. Fresnel Calculation
    float3 F0 = lerp(float3(0.04), albedo, metallic);
    float3 F =
        F0 + (1.0 - F0) * pow(1.0 - max(0.0, dot(worldNormal, viewDir)), 5.0);
    float probSpecular = (F.r + F.g + F.b) / 3.0;
    if (metallic > 0.9) {
      probSpecular = 1.0;
    }

    float3 weight = float3(0);
    // B. Russian Roulette
    float rndVal = rnd(seed);
    bool isSpecular = rndVal < probSpecular;
    if (isSpecular) {
      // Specular Reflection
      float3 reflectDir = reflect(-viewDir, worldNormal);
      float3 jitter = normalize(
          float3(rnd(seed), rnd(seed), rnd(seed)) *
              2.0 -
          1.0);
      bounceDir = normalize(reflectDir + jitter * roughness * roughness);

      if (dot(bounceDir, worldNormal) < 0)
        bounceDir = reflectDir;
      weight *= safe_div(F, probSpecular);
    } else {
      bounceDir = sampleCosineWeightedHemisphere(worldNormal, seed);
      weight *= albedo / max(EPSILON, (1.0 - probSpecular));
    }

    return weight;
  }
}

    //-----------------------------------------------------------------------
    // CLOSEST HIT SHADER - PBR Path Tracer + NEE
    //-----------------------------------------------------------------------
    [shader("closesthit")] void
    rchit_gltf(inout HitPayload payload,
               in BuiltInTriangleIntersectionAttributes attr) {
  HitState hit = getHitState(attr);

  const float3 worldPos = hit.worldPos;
  const float3 worldNormal = hit.worldNormal;
  const float3 viewDir = hit.viewDir;
  const float2 uv = hit.uv;
  const Material material = hit.material;

  // --- GATHER MATERIAL PROPERTIES ---
  float3 albedo = material.baseColorFactor.xyz;
  float metallic = material.metallicFactor;
  float roughness = material.roughnessFactor;
  float3 emissive = material.emission;

  // Textures
  if (material.baseColorTextureIndex > 0) {
    albedo *= textures[material.baseColorTextureIndex].SampleLevel(uv, 0).xyz;
  }
  payload.color += emissive * payload.weight;

  // --- (Direct Lighting) ---
  PbrEvaluator pbr = PbrEvaluator(viewDir, albedo, roughness, metallic);
  const SceneInfo *sceneInfo = pushConst.sceneInfoAddress;
  payload.color += sampleDirectLighting(sceneInfo, worldPos, worldNormal, pbr) *
                   payload.weight;

  // --- (Indirect Lighting) ---
  float3 bounceDir;
  payload.weight *= pbr.sample(viewDir, worldNormal, payload.seed, bounceDir);

  payload.nextRayOrigin = worldPos;
  payload.nextRayDir = bounceDir;
  payload.stop = false;
  payload.emit = false;
}
