#ifndef RAY_COMMON_H_SLANG
#define RAY_COMMON_H_SLANG 1

#ifndef  RAY_TRACE
#define RAY_TRACE 1
#endif

[[vk::binding(BindRayTrace::eTlas, 1)]]        RaytracingAccelerationStructure topLevelAS;
[[vk::binding(BindRayTrace::eOutImage, 1)]]    RWTexture2D<float4> outImage;
[[vk::binding(BindRayTrace::eAccumImage, 1)]]  RWTexture2D<float4> accumImage;
[[vk::binding(BindRayTrace::eAlbedoImage, 1)]] RWTexture2D<float4> albedoImage;
[[vk::binding(BindRayTrace::eNormalImage, 1)]] RWTexture2D<float4> normalImage;

#include "shared/structs.h"
#include "shared/bindings.h"
#include "include/constants.h.slang"
#include "include/core/math.h.slang"
#include "include/core/rnd.h.slang"
#include "include/core/sampler.h.slang"
#include "include/geometry/vertex_fetch.h.slang"
#include "include/light/lights.h.slang"
#include "include/raytrace/trace.h.slang"



#endif
