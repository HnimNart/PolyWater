#ifndef RAY_COMMON_H_SLANG
#define RAY_COMMON_H_SLANG 1

#ifndef  RAY_TRACE
#define RAY_TRACE 1
#endif

// --- Scene bindings --- 
[[vk::binding(BindRayTrace::eTlas, 1)]]      RaytracingAccelerationStructure topLevelAS;
[[vk::binding(BindRayTrace::eOutImage, 1)]]   RWTexture2D<float4> outImage;
[[vk::binding(BindRayTrace::eAccumImage, 1)]] RWTexture2D<float4> accumImage;

#include "shared/structs.h"      // Defines Material, Light, HitPayload
#include "shared/bindings.h"     // Defines Descriptor Sets, topLevelAS
#include "include/constants.h.slang"
#include "include/core/math.h.slang"
#include "include/core/rnd.h.slang"
#include "include/core/sampler.h.slang"
#include "include/geometry/vertex_fetch.h.slang" // Uses shared structs & bindings
#include "include/light/lights.h.slang"          // Uses shared structs & math
#include "include/raytrace/trace.h.slang"        // Uses bindings (AS) & structs (Payload)



#endif
