#pragma once

NAMESPACE_SHADERIO_BEGIN()

// ==========================================
// SET 0: GLOBAL
// ==========================================
enum BindGlobal
{
  eTextures = 0,  // Bindless array
};

// ==========================================
// SET 1: PASS-SPECIFIC (Swappable at Runtime)
// ==========================================

// For Ray Tracing Shaders (.rgen / .rchit)
enum BindRayTrace
{
  eTlas = 0,
  eOutImage = 1,
  eAccumImage = 2,
  eAlbedoImage = 3,
  eNormalImage = 4
};

// For Meshlet/Raster Shaders (.mesh / .frag)
enum BindRaster
{
  eHiZTexture = 0,
  eHiZSampler = 1,
  eShadowMap = 2,
  eShadowSampler = 3
};

#ifndef __cplusplus
// clang-format off
// Push constants containing scene information, camera data
[[vk::push_constant]]                           ConstantBuffer<PushConstant> pushConst;

// --- SHARED GLOBALS ---
[[vk::binding(BindGlobal::eTextures, 0)]] Sampler2D textures[];

// --- RASTER PASS SET 1 ---
[[vk::binding(BindRaster::eShadowMap, 1)]] Texture2D<float> shadowMap;
[[vk::binding(BindRaster::eShadowSampler, 1)]] SamplerComparisonState shadowSampler;

// clang-format on
#endif

NAMESPACE_SHADERIO_END()
