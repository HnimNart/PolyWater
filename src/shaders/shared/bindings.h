#pragma once

NAMESPACE_SHADERIO_BEGIN()

// ==========================================
// SET 0: GLOBAL 
// ==========================================
enum BindGlobal {
    eTextures = 0,  // Bindless array
};

// ==========================================
// SET 1: PASS-SPECIFIC (Swappable at Runtime)
// ==========================================

// For Ray Tracing Shaders (.rgen / .rchit)
enum BindRayTrace {
    eTlas     = 0,
    eOutImage    = 1,
    eAccumImage  = 2
};

// For Meshlet/Raster Shaders (.mesh / .frag)
enum BindRaster {
    eHiZTexture  = 0,
    eHiZSampler  = 1
};

#ifndef MAX_SCENE_TEXTURES
#define MAX_SCENE_TEXTURES 4096
#endif

#ifndef __cplusplus
// clang-format off
// Push constants containing scene information, camera data
[[vk::push_constant]]                           ConstantBuffer<PushConstant> pushConst;

// --- SHARED GLOBALS ---
// Fixed-size array required for Metal (MSL does not support flexible/unbounded
// arrays of opaque resource types such as texture2d in a struct).
[[vk::binding(BindGlobal::eTextures, 0)]] Sampler2D textures[MAX_SCENE_TEXTURES];

// clang-format on
#endif


NAMESPACE_SHADERIO_END()
