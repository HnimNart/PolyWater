#pragma once

NAMESPACE_SHADERIO_BEGIN()

// Binding Points
enum BindingPoints {
  eTlas = 0,       // Top-level acceleration structure
  eOutImage = 1,   // Binding point for output image
  eAccumImage = 2, //
  eHiZTexture = 3, // NEW: Hi-Z depth texture (Set 1)
  eHiZSampler = 4, // NEW: Hi-Z sampler (Set 1)
  eTextures = 5,   // Binding point for textures
};

#ifndef __cplusplus
// clang-format off
// Push constants containing scene information, camera data, and material overrides
[[vk::push_constant]]                           ConstantBuffer<PushConstant> pushConst;

// ==========================================
// SET 0: Global Shared Resources
// ==========================================
[[vk::binding(BindingPoints::eTextures, 0)]] Sampler2D textures[]; 

// ==========================================
// SET 1: Pass-Specific Resources
// ==========================================
// Ray Tracing
[[vk::binding(BindingPoints::eTlas, 1)]]       RaytracingAccelerationStructure topLevelAS;
[[vk::binding(BindingPoints::eOutImage, 1)]]   RWTexture2D<float4> outImage;
[[vk::binding(BindingPoints::eAccumImage, 1)]] RWTexture2D<float4> accumImage;

// Rasterization (Meshlet Pass)
[[vk::binding(BindingPoints::eHiZTexture, 1)]] Texture2D<float> hiZTexture;
[[vk::binding(BindingPoints::eHiZSampler, 1)]] SamplerState hiZSampler;

// clang-format on
#endif

#define MAX_SCENE_TEXTURES 4096
#define MAX_SCENE_MESHLETS (1000000)

NAMESPACE_SHADERIO_END()
