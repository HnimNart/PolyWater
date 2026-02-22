#pragma once

#ifndef __cplusplus
// clang-format off
// Push constants containing scene information, camera data, and material overrides
[[vk::push_constant]]                           ConstantBuffer<PushConstant> pushConst;
// Texture array for material textures (albedo, normal maps, etc.)
[[vk::binding(BindingPoints::eTextures, 0)]]    Sampler2D textures[];
// Top-level acceleration structure containing the scene geometry hierarchy
[[vk::binding(BindingPoints::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
// Output image where the final rendered result will be stored
[[vk::binding(BindingPoints::eOutImage, 1)]]    RWTexture2D<float4> outImage;
// Output image where the final rendered result will be stored
[[vk::binding(BindingPoints::eAccumImage, 1)]]    RWTexture2D<float4> accumImage;
// clang-format on
#endif

#define MAX_SCENE_TEXTURES 4096
